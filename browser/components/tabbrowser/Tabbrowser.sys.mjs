/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  NewTabPagePreloading:
    "moz-src:///browser/components/tabbrowser/NewTabPagePreloading.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ReducedProtectionNotification:
    "resource:///modules/ReducedProtectionNotification.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
  TabCrashHandler: "resource:///modules/ContentCrashHandlers.sys.mjs",
  webrtcUI: "resource:///modules/webrtcUI.sys.mjs",
});

/**
 * A set of known icons to use for internal pages. These are hardcoded so we can
 * start loading them faster than FaviconLoader would normally find them.
 */
const FAVICON_DEFAULTS = {
  "about:newtab": "chrome://branding/content/icon32.png",
  "about:home": "chrome://branding/content/icon32.png",
  "about:welcome": "chrome://branding/content/icon32.png",
  "about:privatebrowsing": "chrome://browser/skin/privatebrowsing/favicon.svg",
  "chrome://browser/content/aiwindow/aiWindow.html":
    "chrome://browser/skin/smart-window-simplified.svg",
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
const TAB_LABEL_MAX_LENGTH = 256;

/**
 * Updates the User Context UI indicators if the browser is in a non-default context
 */
async function getTotalMemoryUsage() {
  const procInfo = await ChromeUtils.requestProcInfo();
  let totalMemoryUsage = procInfo.memory;
  for (const child of procInfo.children) {
    totalMemoryUsage += child.memory;
  }
  return totalMemoryUsage;
}

// Used for links dropped on browser content areas. Can be called with a null
// event argument when dropping links in the content process.
// handleDroppedLink has the following 2 overloads:
//   handleDroppedLink(tabbrowser, browser, event, url, name, triggeringPrincipal)
//   handleDroppedLink(tabbrowser, browser, event, links, triggeringPrincipal)
async function handleDroppedLink(
  tabbrowser,
  browser,
  event,
  urlOrLinks,
  nameOrTriggeringPrincipal,
  triggeringPrincipal
) {
  // If links are dropped in content process, event.preventDefault() should be
  // called in the content process. Otherwise, we do it here.
  if (event) {
    // Keep the event from being handled by the dragDrop listeners
    // built-in to gecko if they happen to be above us.
    event.preventDefault();
  }
  let links;
  if (Array.isArray(urlOrLinks)) {
    links = urlOrLinks;
    triggeringPrincipal = nameOrTriggeringPrincipal;
  } else {
    links = [{ url: urlOrLinks, nameOrTriggeringPrincipal, type: "" }];
  }

  let lastLocationChange = browser.lastLocationChange;

  let userContextId = browser.getAttribute("usercontextid");

  // event is null if links are dropped in content process.
  // inBackground should be false, as it's loading into current browser.
  let inBackground = false;
  if (event) {
    inBackground = Services.prefs.getBoolPref("browser.tabs.loadInBackground");
    if (event.shiftKey) {
      inBackground = !inBackground;
    }
  }

  if (
    links.length >= Services.prefs.getIntPref("browser.tabs.maxOpenBeforeWarn")
  ) {
    // Sync dialog cannot be used inside drop event handler.
    let answer = await tabbrowser.OpenInTabsUtils.promiseConfirmOpenInTabs(
      links.length,
      tabbrowser.documentGlobal
    );
    if (!answer) {
      return;
    }
  }

  let urls = [];
  let postDatas = [];
  for (let link of links) {
    let data = await tabbrowser.UrlbarUtils.getShortcutOrURIAndPostData(
      link.url
    );
    urls.push(data.url);
    postDatas.push(data.postData);
  }
  if (lastLocationChange == browser.lastLocationChange) {
    tabbrowser.loadTabs(urls, {
      inBackground,
      replace: true,
      allowThirdPartyFixup: false,
      postDatas,
      userContextId,
      targetTab: tabbrowser.getTabForBrowser(browser),
      triggeringPrincipal,
    });
  }
}

/**
 * Displays and manages the contents of a browser window's tabs. Every window
 * instantiates this class during startup and exposes the instance in its window
 * scope as `gBrowser`.
 */
export class Tabbrowser {
  static create(window) {
    window.gBrowser = new Tabbrowser(window);
    window.gBrowser.init();
  }

  static destroy(window) {
    window.gBrowser.destroy();
  }

  init() {
    this.tabContainer = this.document.getElementById("tabbrowser-tabs");
    this.tabGroupMenu = this.document.getElementById("tab-group-editor");
    this.tabNoteMenu = this.document.getElementById("tab-note-menu");
    this.tabbox = this.document.getElementById("tabbrowser-tabbox");
    this.tabpanels = this.document.getElementById("tabbrowser-tabpanels");
    this.pinnedTabsContainer = this.document.getElementById(
      "pinned-tabs-container"
    );
    this.splitViewCommandSet =
      this.document.getElementById("splitViewCommands");

    // Defined on the instance, not on `lazy`, because callers reach these
    // as `gBrowser.TabMetrics` and friends.
    // eslint-disable-next-line mozilla/lazy-getter-object-name
    ChromeUtils.defineESModuleGetters(this, {
      ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
      AsyncTabSwitcher:
        "moz-src:///browser/components/tabbrowser/AsyncTabSwitcher.sys.mjs",
      OpenInTabsUtils:
        "moz-src:///browser/components/tabbrowser/OpenInTabsUtils.sys.mjs",
      PictureInPicture:
        "moz-src:///toolkit/components/pictureinpicture/PictureInPicture.sys.mjs",
      SmartTabGroupingManager:
        "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs",
      SponsorProtection:
        "moz-src:///browser/components/newtab/SponsorProtection.sys.mjs",
      TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
      TabStateFlusher:
        "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
      TaskbarTabsUtils:
        "resource:///modules/taskbartabs/TaskbarTabsUtils.sys.mjs",
      TaskbarTabs: "resource:///modules/taskbartabs/TaskbarTabs.sys.mjs",
      UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
      UrlbarProviderOpenTabs:
        "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
      FaviconUtils: "moz-src:///toolkit/modules/FaviconUtils.sys.mjs",
      KeyboardLockUtils: "resource://gre/modules/KeyboardLockUtils.sys.mjs",
    });
    ChromeUtils.defineLazyGetter(this, "tabLocalization", () => {
      return new Localization(
        [
          "browser/tabbrowser.ftl",
          "browser/taskbartabs.ftl",
          "branding/brand.ftl",
        ],
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
      "_tabNotesEnabled",
      "browser.tabs.notes.enabled",
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
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_remoteSVGIconDecoding",
      "browser.tabs.remoteSVGIconDecoding",
      false
    );

    if (AppConstants.MOZ_CRASHREPORTER) {
      // eslint-disable-next-line mozilla/lazy-getter-object-name
      ChromeUtils.defineESModuleGetters(this, {
        TabCrashHandler: "resource:///modules/ContentCrashHandlers.sys.mjs",
      });
    }

    Services.obs.addObserver(this, "contextual-identity-updated");
    Services.obs.addObserver(this, "intl:app-locales-changed");

    this.document.addEventListener("keydown", this, {
      mozSystemGroup: true,
    });
    this.document.addEventListener("keypress", this, {
      mozSystemGroup: true,
    });
    this.document.addEventListener("visibilitychange", this);
    this.documentGlobal.addEventListener("framefocusrequested", this);
    this.documentGlobal.addEventListener("activate", this);
    this.documentGlobal.addEventListener("deactivate", this);
    this.documentGlobal.addEventListener("TabGroupCollapse", this);
    this.documentGlobal.addEventListener("TabGroupCreateByUser", this);
    this.documentGlobal.addEventListener("TabGrouped", this);
    this.documentGlobal.addEventListener("TabUngrouped", this);
    this.documentGlobal.addEventListener("TabSplitViewActivate", this);
    this.documentGlobal.addEventListener("TabSplitViewDeactivate", this);

    this.documentGlobal
      .matchMedia("(prefers-color-scheme: dark)")
      .addEventListener("change", this);

    this.tabContainer.init();

    this._defaultDropLinkHandler = function (...args) {
      // The droppedLinkHandler gets invoked with `this` being the browser
      // element on which the drop took place.
      let browser = this;
      let tabbrowser = browser.getTabBrowser();
      handleDroppedLink(tabbrowser, browser, ...args);
    };
    this._setupInitialBrowserAndTab();

    if (Services.prefs.getIntPref("browser.display.document_color_use") == 2) {
      this.tabpanels.style.backgroundColor = Services.prefs.getCharPref(
        "browser.display.background_color"
      );
    }

    this._setFindbarData();

    // We take over setting the document title, so remove the l10n id to
    // avoid it being re-translated and overwriting document content if
    // we ever switch languages at runtime.
    this.document.querySelector("title").removeAttribute("data-l10n-id");

    this._setupEventListeners();
    this._initialized = true;
  }

  documentGlobal;

  document;

  // `ownerDocument` stays available to consumers outside this module, which
  // are updated separately.
  get ownerDocument() {
    return this.document;
  }

  constructor(window) {
    this.documentGlobal = window;
    this.document = window.document;
  }

  closingTabsEnum = {
    ALL: 0,
    OTHER: 1,
    TO_START: 2,
    TO_END: 3,
    MULTI_SELECTED: 4,
    DUPLICATES: 6,
    ALL_DUPLICATES: 7,
  };

  #lastRelatedTabMap = new WeakMap();

  #progressListeners = [];

  #tabsProgressListeners = [];

  #tabListeners = new Map();

  #tabFilters = new Map();

  _isBusy = false;

  _awaitingToggleCaretBrowsingPrompt = false;

  #previewMode = false;

  _lastFindValue = "";

  _tabLayerCache = [];

  tabAnimationsInProgress = 0;

  /**
   * Binding from browser to tab
   */
  #tabForBrowser = new WeakMap();

  /**
   * `_createLazyBrowser` will define properties on the unbound lazy browser
   * which correspond to properties defined in MozBrowser which will be bound to
   * the browser when it is inserted into the document.  If any of these
   * properties are accessed by consumers, `_insertBrowser` is called and
   * the browser is inserted to ensure that things don't break.  This list
   * provides the names of properties that may be called while the browser
   * is in its unbound (lazy) state.
   */
  #browserBindingProperties = [
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

  #lastMultiSelectedTabRef = null;

  #clearMultiSelectionLocked = false;

  #clearMultiSelectionLockedOnce = false;

  #multiSelectChangeStarted = false;

  #multiSelectChangeAdditions = new Set();

  #multiSelectChangeRemovals = new Set();

  #multiSelectChangeSelected = false;

  /**
   * Tab close requests are ignored if the window is closing anyway,
   * e.g. when holding Ctrl+W.
   */
  #windowIsClosing = false;

  preloadedBrowser = null;

  /**
   * This defines a proxy which allows us to access browsers by
   * index without actually creating a full array of browsers.
   */
  browsers = new Proxy([], {
    has: (target, name) => {
      if (typeof name == "string" && Number.isInteger(parseInt(name))) {
        return name in this.tabs;
      }
      return false;
    },
    get: (target, name) => {
      if (name == "length") {
        return this.tabs.length;
      }
      if (typeof name == "string" && Number.isInteger(parseInt(name))) {
        if (!(name in this.tabs)) {
          return undefined;
        }
        return this.tabs[name].linkedBrowser;
      }
      return target[name];
    },
  });

  /**
   * List of browsers whose docshells must be active in order for print preview
   * to work.
   */
  _printPreviewBrowsers = new Set();

  /** @type {MozTabSplitViewWrapper} */
  #activeSplitView = null;

  get activeSplitView() {
    return this.#activeSplitView;
  }

  /**
   * List of browsers which are currently in an active Split View.
   *
   * @type {MozBrowser[]}
   */
  get splitViewBrowsers() {
    const browsers = [];
    if (this.#activeSplitView) {
      for (const tab of this.#activeSplitView.tabs) {
        browsers.push(tab.linkedBrowser);
      }
    }
    return browsers;
  }

  _switcher = null;

  /**
   * @type {Array<{count: number, uris: [string, string], timestamp: number}>}
   */
  #tabSelectTimestamps = [];

  get tabs() {
    return this.tabContainer.allTabs;
  }

  get tabGroups() {
    return this.tabContainer.allGroups;
  }

  get splitViews() {
    return this.tabContainer.allSplitViews;
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
   * Records a tab interaction metric.
   *
   * @param {string} action - The action from TabMetrics.METRIC_ACTION.
   * @param {TabMetricsContext} [metricsContext] - Context for the metric.
   * @param {object} [options]
   * @param {number} [options.tabCount] - Number of tabs involved. Defaults to the number of selected tabs.
   */
  recordTabMetrics(
    action,
    metricsContext,
    { tabCount = this.selectedTabs.length } = {}
  ) {
    // We only report user triggered events and not decomposed events.
    if (!metricsContext?.isUserTriggered || metricsContext.isDecomposed) {
      return;
    }

    Glean.tab.actions[action].add(1);
    Glean.tab.tabCount[action].add(tabCount);

    Glean.tab.interaction.record({
      action,
      source: metricsContext.telemetrySource,
      tab_count: tabCount,
      layout: this.tabContainer.verticalMode ? "vertical" : "horizontal",
    });
  }

  /**
   * Returns all tabs in the current window, including hidden tabs and tabs
   * in collapsed groups, but excluding closing tabs and the Firefox View tab.
   */
  get openTabs() {
    return this.tabContainer.openTabs;
  }

  /**
   * Same as `openTabs` but excluding hidden tabs.
   */
  get nonHiddenTabs() {
    return this.tabContainer.nonHiddenTabs;
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

  setSelectedTab(val, metricsContext = null) {
    if (this.selectedTab === val) {
      return;
    }

    if (
      this.documentGlobal.gSharedTabWarning.willShowSharedTabWarning(val) ||
      this.document.documentElement.hasAttribute("window-modal-open") ||
      (this.documentGlobal.gNavToolbox.collapsed && !this._allowTabChange)
    ) {
      return;
    }
    // Update the tab
    this.tabbox.selectedTab = val;

    this.recordTabMetrics(
      this.TabMetrics.METRIC_ACTION.ACTIVATE,
      metricsContext
    );
  }

  set selectedTab(val) {
    this.setSelectedTab(val);
  }

  get selectedTab() {
    return this._selectedTab;
  }

  get selectedBrowser() {
    return this._selectedBrowser;
  }

  get selectedBrowsers() {
    const splitViewBrowsers = this.splitViewBrowsers;
    return splitViewBrowsers.length
      ? splitViewBrowsers
      : [this._selectedBrowser];
  }

  _setupInitialBrowserAndTab() {
    // See browser.js for the meaning of window.arguments.
    // Bug 1485961 covers making this more sane.
    let userContextId =
      this.documentGlobal.arguments && this.documentGlobal.arguments[5];

    let openWindowInfo = this.documentGlobal.docShell.treeOwner
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIAppWindow).initialOpenWindowInfo;

    if (
      !openWindowInfo &&
      this.documentGlobal.arguments &&
      this.documentGlobal.arguments[11]
    ) {
      openWindowInfo = this.documentGlobal.arguments[11];
    }

    let extraOptions;
    if (this.documentGlobal.arguments?.[1] instanceof Ci.nsIPropertyBag2) {
      extraOptions = this.documentGlobal.arguments[1];
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

    let tabArgument = this.documentGlobal.gBrowserInit.getTabToAdopt();

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

    if (openWindowInfo) {
      userContextId = openWindowInfo.originAttributes.userContextId;
    }

    let remoteTypeOptions = { window: this.documentGlobal, userContextId };
    if (triggeringRemoteType) {
      // NOTE: We intentionally don't allow setting preferredRemoteType to
      // NOT_REMOTE (null), as we don't want to choose the parent process.
      remoteTypeOptions.preferredRemoteType = triggeringRemoteType;
    }

    if (tabArgument && tabArgument.linkedBrowser) {
      remoteType = tabArgument.linkedBrowser.remoteType;
      initialBrowsingContextGroupId =
        tabArgument.linkedBrowser.browsingContext?.group.id;
    } else if (openWindowInfo) {
      if (openWindowInfo.isRemote) {
        remoteType = ChromeUtils.predictRemoteTypeForURI(
          null,
          remoteTypeOptions
        );
      } else {
        remoteType = lazy.E10SUtils.NOT_REMOTE;
      }
    } else {
      let uriToLoad = this.documentGlobal.gBrowserInit.uriToLoadPromise;
      if (uriToLoad && Array.isArray(uriToLoad)) {
        uriToLoad = uriToLoad[0]; // we only care about the first item
      }

      if (uriToLoad && typeof uriToLoad == "string") {
        remoteType = ChromeUtils.predictRemoteTypeForURI(
          uriToLoad,
          remoteTypeOptions
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
        remoteType = lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE;
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
    if (this.documentGlobal.gBrowserAllowScriptsToCloseInitialTabs) {
      browser.setAttribute("allowscriptstoclose", "true");
    }
    browser.droppedLinkHandler = this._defaultDropLinkHandler;
    browser.loadURI = URILoadingWrapper.loadURI.bind(
      URILoadingWrapper,
      browser
    );
    browser.fixupAndLoadURIString =
      URILoadingWrapper.fixupAndLoadURIString.bind(URILoadingWrapper, browser);

    if (lazy.AIWindow.isAIWindowActive(this.documentGlobal)) {
      let uriToLoad = this.documentGlobal.gBrowserInit.uriToLoadPromise;
      let firstURI = Array.isArray(uriToLoad) ? uriToLoad[0] : uriToLoad;

      if (!this._allowTransparentBrowser) {
        // firstURI may be a Promise (uriToLoadPromise still resolving while
        // SessionStore restores) or empty; only build a URI from a real
        // string, otherwise default to transparent like the no-URI case.
        browser.toggleAttribute(
          "transparent",
          !firstURI ||
            typeof firstURI != "string" ||
            lazy.AIWindow.isAIWindowContentPage(Services.io.newURI(firstURI))
        );
      }
    }

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
      lazy.ContextualIdentityService.setTabStyle(tab);
    }
    this.#updateUserContextUIIndicator();

    this.#tabForBrowser.set(browser, tab);

    this.appendStatusPanel();

    // This is the initial browser, so it's usually active; the default is false
    // so we have to update it:
    browser.docShellIsActive = this.shouldActivateDocShell(browser);

    // Hook the browser up with a progress listener.
    let tabListener = new TabProgressListener(tab, browser, true, false);
    let filter = Cc[
      "@mozilla.org/appshell/component/browser-status-filter;1"
    ].createInstance(Ci.nsIWebProgress);
    filter.addProgressListener(tabListener, Ci.nsIWebProgress.NOTIFY_ALL);
    this.#tabListeners.set(tab, tabListener);
    this.#tabFilters.set(tab, filter);
    browser.webProgress.addProgressListener(
      filter,
      Ci.nsIWebProgress.NOTIFY_ALL
    );
  }

  #updateUserContextUIIndicator() {
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

    let hbox = this.document.getElementById("userContext-icons");

    let userContextId = this.selectedBrowser.getAttribute("usercontextid");
    if (!userContextId) {
      // The container-creation panel can temporarily reveal this indicator to
      // use it as its anchor; don't hide it again while that panel is up.
      let creationPanel = this.document.getElementById(
        "containerCreation-panel"
      );
      if (creationPanel && creationPanel.state != "closed") {
        return;
      }
      replaceContainerClass("color", hbox, "");
      hbox.hidden = true;
      return;
    }

    let identity =
      lazy.ContextualIdentityService.getPublicIdentityFromId(userContextId);
    if (!identity) {
      replaceContainerClass("color", hbox, "");
      hbox.hidden = true;
      return;
    }

    replaceContainerClass("color", hbox, identity.color);

    let label =
      lazy.ContextualIdentityService.getUserContextLabel(userContextId);
    this.document.getElementById("userContext-label").textContent = label;
    // Also set the container label as the tooltip so we can only show the icon
    // in small windows.
    hbox.setAttribute("tooltiptext", label);

    let indicator = this.document.getElementById("userContext-indicator");
    replaceContainerClass("icon", indicator, identity.icon);

    hbox.hidden = false;
  }

  // Begin forwarded browser properties. If you add a property to the browser
  // element, consider adding it here as well.

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

  reloadWithFlags(reloadFlags) {
    const unchangedRemoteness = [];

    for (const tab of this.selectedTabs) {
      const browser = tab.linkedBrowser;
      const url = browser.currentURI;
      const urlSpec = url.spec;
      // We need to cache the content principal here because the browser will be
      // reconstructed when the remoteness changes and the content prinicpal will
      // be cleared after reconstruction.
      const principal = tab.linkedBrowser.contentPrincipal;
      if (this.updateBrowserRemotenessByURL(browser, urlSpec)) {
        // If the remoteness has changed, the new browser doesn't have any
        // information of what was loaded before, so we need to load the previous
        // URL again.
        if (tab.linkedPanel) {
          loadBrowserURI(browser, url, principal);
        } else {
          // Shift to fully loaded browser and make
          // sure load handler is instantiated.
          tab.addEventListener(
            "SSTabRestoring",
            () => loadBrowserURI(browser, url, principal),
            { once: true }
          );
          this._insertBrowser(tab);
        }
      } else {
        unchangedRemoteness.push(tab);
      }
    }

    if (!unchangedRemoteness.length) {
      return;
    }

    // Reset temporary permissions on the remaining tabs to reload.
    // This is done here because we only want to reset
    // permissions on user reload.
    for (const tab of unchangedRemoteness) {
      lazy.SitePermissions.clearTemporaryBlockPermissions(tab.linkedBrowser);
      // Also reset DOS mitigations for the basic auth prompt on reload.
      delete tab.linkedBrowser.authPromptAbuseCounter;
    }
    this.documentGlobal.gIdentityHandler.hidePopup();
    this.documentGlobal.gPermissionPanel.hidePopup();

    if (this.document.hasValidTransientUserGestureActivation) {
      reloadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_USER_ACTIVATION;
    }

    for (const tab of unchangedRemoteness) {
      lazy.ReducedProtectionNotification.markUserReload(tab.linkedBrowser);
      reloadBrowser(tab, this);
    }

    function reloadBrowser(tab, tabbrowser) {
      if (tab.linkedPanel) {
        const { browsingContext } = tab.linkedBrowser;
        const { sessionHistory } = browsingContext;
        if (sessionHistory) {
          sessionHistory.reload(reloadFlags);
        } else {
          browsingContext.reload(reloadFlags);
        }
      } else {
        // Shift to fully loaded browser and make
        // sure load handler is instantiated.
        tab.addEventListener(
          "SSTabRestoring",
          () => tab.linkedBrowser.browsingContext.reload(reloadFlags),
          {
            once: true,
          }
        );
        tabbrowser._insertBrowser(tab);
      }
    }

    function loadBrowserURI(browser, url, principal) {
      browser.loadURI(url, {
        loadFlags: reloadFlags,
        triggeringPrincipal: principal,
      });
    }
  }

  stop() {
    return this.selectedBrowser.stop();
  }

  /**
   * throws exception for unknown schemes
   *
   * @param {nsIURI} uri
   * @param {LoadURIOptions} [params]
   */
  loadURI(uri, params) {
    return this.selectedBrowser.loadURI(uri, params);
  }
  /**
   * throws exception for unknown schemes
   *
   * @param {string} uriString
   * @param {LoadURIOptions} [params]
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
      let keyEl = this.document.getElementById("key_find");
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
   *
   * @param {MozTabbrowserTab} [aTab]
   *   Defaults to the selected tab.
   */
  getCachedFindBar(aTab = this.selectedTab) {
    return aTab._findBar;
  }

  /**
   * Get the findbar, and create it if it doesn't exist.
   *
   * @param {MozTabbrowserTab} [aTab]
   *   Defaults to the selected tab.
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
   *
   * @param {MozTabbrowserTab} aTab the tab to create the find bar for.
   * @return the created findbar, or null if the window or tab is closed/closing.
   */
  async _createFindBar(aTab) {
    let findBar = this.document.createXULElement("findbar");
    let browser = this.getBrowserForTab(aTab);

    browser.parentNode.insertAdjacentElement("afterend", findBar);

    await new Promise(r => this.documentGlobal.requestAnimationFrame(r));
    delete aTab._pendingFindBar;
    if (this.documentGlobal.closed || aTab.closing) {
      return null;
    }

    findBar.browser = browser;
    findBar._findField.value = this._lastFindValue;

    aTab._findBar = findBar;

    let event = this.document.createEvent("Events");
    event.initEvent("TabFindInitialized", true, false);
    aTab.dispatchEvent(event);

    return findBar;
  }

  appendStatusPanel(browser = this.selectedBrowser) {
    browser.insertAdjacentElement(
      "afterend",
      this.documentGlobal.StatusPanel.panel
    );
  }

  _updateTabBarForPinnedTabs() {
    this.tabContainer._unlockTabSizing();
    this.tabContainer._handleTabSelect(true);
    this.tabContainer._updateCloseButtons();
  }

  #notifyPinnedStatus(
    aTab,
    { metricsContext = this.TabMetrics.UNKNOWN_CONTEXT } = {}
  ) {
    // browsingContext is expected to not be defined on discarded tabs.
    if (aTab.linkedBrowser.browsingContext) {
      aTab.linkedBrowser.browsingContext.isAppTab = aTab.pinned;
    }

    this.recordTabMetrics(
      aTab.pinned
        ? this.TabMetrics.METRIC_ACTION.PIN
        : this.TabMetrics.METRIC_ACTION.UNPIN,
      metricsContext,
      { tabCount: 1 }
    );

    let event = new this.documentGlobal.CustomEvent(
      aTab.pinned ? "TabPinned" : "TabUnpinned",
      {
        bubbles: true,
        cancelable: false,
        detail: { metricsContext },
      }
    );
    aTab.dispatchEvent(event);
  }

  /**
   * Pin a tab.
   *
   * @param {MozTabbrowserTab} aTab
   *   The tab to pin.
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes, defaults to an unknown context.
   */
  pinTab(aTab, { metricsContext = this.TabMetrics.UNKNOWN_CONTEXT } = {}) {
    if (aTab.pinned || aTab == this.documentGlobal.FirefoxViewHandler.tab) {
      return;
    }

    this.showTab(aTab);
    this.#handleTabMove(aTab, () => {
      let periphery = this.document.getElementById(
        "pinned-tabs-container-periphery"
      );
      // If periphery is null, append to end
      this.pinnedTabsContainer.insertBefore(aTab, periphery);
    });

    aTab.setAttribute("pinned", "true");
    this._updateTabBarForPinnedTabs();
    this.#notifyPinnedStatus(aTab, { metricsContext });
  }

  /**
   * Unpin a tab.
   *
   * @param {MozTabbrowserTab} aTab
   *   The tab to pin.
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes, defaults to an unknown context.
   */
  unpinTab(aTab, { metricsContext = this.TabMetrics.UNKNOWN_CONTEXT } = {}) {
    if (!aTab.pinned) {
      return;
    }

    this.#handleTabMove(aTab, () => {
      // we remove this attribute first, so that allTabs represents
      // the moving of a tab from the pinned tabs container
      // and back into arrowscrollbox.
      aTab.removeAttribute("pinned");
      this.tabContainer.arrowScrollbox.prepend(aTab);
    });

    aTab.style.marginInlineStart = "";
    aTab._pinnedUnscrollable = false;
    this._updateTabBarForPinnedTabs();
    this.#notifyPinnedStatus(aTab, { metricsContext });
  }

  previewTab(aTab, aCallback) {
    let currentTab = this.selectedTab;
    try {
      // Suppress focus, ownership and selected tab changes
      this.#previewMode = true;
      this.selectedTab = aTab;
      aCallback();
    } finally {
      this.selectedTab = currentTab;
      this.#previewMode = false;
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
    return this.#tabForBrowser.get(aBrowser);
  }

  getPanel(aBrowser) {
    return this.getBrowserContainer(aBrowser).parentNode;
  }

  getBrowserContainer(aBrowser) {
    return (aBrowser || this.selectedBrowser).parentNode.parentNode;
  }

  getTabNotificationDeck() {
    if (!this._tabNotificationDeck) {
      let template = this.document.getElementById(
        "tab-notification-deck-template"
      );
      template.replaceWith(template.content);
      this._tabNotificationDeck = this.document.getElementById(
        "tab-notification-deck"
      );
    }
    return this._tabNotificationDeck;
  }

  #nextNotificationBoxId = 0;
  getNotificationBox(aBrowser) {
    let browser = aBrowser || this.selectedBrowser;
    if (!browser._notificationBox) {
      browser._notificationBox =
        new this.documentGlobal.MozElements.NotificationBox(element => {
          element.setAttribute("notificationside", "top");
          element.setAttribute(
            "name",
            `tab-notification-box-${this.#nextNotificationBoxId++}`
          );
          this.#insertNotificationBox(browser, element);
        }, this._notificationEnableDelay);
    }
    return browser._notificationBox;
  }

  /**
   * Insert a notification box into the correct container.
   *
   * In Split View, notifications go into the panel's browserContainer
   * so they appear only in that panel.
   *
   * Otherwise, they go into the shared tab notification deck.
   *
   * @param {MozBrowser} browser
   * @param {Element} box
   *   The notification box.
   */
  #insertNotificationBox(browser, box) {
    if (this.#isBrowserInActiveSplitView(browser)) {
      let browserContainer = this.getBrowserContainer(browser);
      if (box.parentNode === browserContainer) {
        // Notification box is already in the container.
        return;
      }
      // Display the notification box as the first item in .browserContainer, so it will
      // be placed before the Responsive Design Mode toolbar when it's displayed.
      browserContainer.prepend(box);
      return;
    }
    this.getTabNotificationDeck().append(box);
    if (browser == this.selectedBrowser) {
      this._updateVisibleNotificationBox(browser);
    }
  }

  #isBrowserInActiveSplitView(browser) {
    let tab = this.getTabForBrowser(browser);
    return this.#activeSplitView && tab?.splitview === this.#activeSplitView;
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
    if (
      notificationBox?._stack &&
      notificationBox._stack.parentNode !== this.getTabNotificationDeck()
    ) {
      // Notification box is not in the deck (e.g. in a Split View panel).
      return;
    }
    this.getTabNotificationDeck().selectedViewName = notificationBox
      ? notificationBox.stack.getAttribute("name")
      : "";
  }

  getTabDialogBox(aBrowser) {
    if (!aBrowser) {
      throw new Error("aBrowser is required");
    }
    if (!aBrowser.tabDialogBox) {
      aBrowser.tabDialogBox = new aBrowser.documentGlobal.TabDialogBox(
        aBrowser
      );
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
      callListeners(this.#progressListeners, aArguments);
    }

    if (aCallTabsListeners) {
      aArguments.unshift(aBrowser);

      callListeners(this.#tabsProgressListeners, aArguments);
    }

    return rv;
  }

  /**
   * Sets an icon for the tab if the URI is defined in FAVICON_DEFAULTS.
   *
   * @param {MozTabbrowserTab} aTab
   * @param {nsIURI} [aURI]
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
    aClearImageFirst = false
  ) {
    let makeString = url => (url instanceof Ci.nsIURI ? url.spec : url);

    aIconURL = makeString(aIconURL);
    aOriginalURL = makeString(aOriginalURL);

    let LOCAL_PROTOCOLS = ["chrome:", "about:", "resource:", "data:"];

    if (
      aIconURL &&
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
        let url = aIconURL;
        if (
          this._remoteSVGIconDecoding &&
          url.startsWith(this.FaviconUtils.SVG_DATA_URI_PREFIX)
        ) {
          url = this.#getMozRemoteImageURLForSvg(browser, url);
        }
        aTab.setAttribute("image", url);
      } else {
        aTab.removeAttribute("image");
      }
      this._tabAttrModified(aTab, ["image"]);
    }

    // The aOriginalURL argument is currently only used by tests.
    this._callProgressListeners(browser, "onLinkIconAvailable", [
      aIconURL,
      aOriginalURL,
    ]);
  }

  // Used for refreshing the icons when the color scheme changes.
  #maybeRefreshIcons() {
    if (!this._remoteSVGIconDecoding) {
      return;
    }

    for (const tab of this.tabs) {
      let browser = this.getBrowserForTab(tab);
      let iconURL = browser.mIconURL;
      if (
        !iconURL ||
        !iconURL.startsWith(this.FaviconUtils.SVG_DATA_URI_PREFIX)
      ) {
        continue;
      }

      tab.setAttribute(
        "image",
        this.#getMozRemoteImageURLForSvg(browser, iconURL)
      );
    }
  }

  #getMozRemoteImageURLForSvg(browser, aUrl) {
    let options = {
      // 16px is hardcoded for .tab-icon-image in tabs.css
      size: Math.floor(16 * this.documentGlobal.devicePixelRatio),
      colorScheme: this.documentGlobal.matchMedia(
        "(prefers-color-scheme: dark)"
      ).matches
        ? "dark"
        : "light",
    };

    // Use the tab's child process (if available) to load and render the favicon.
    let contentParentId =
      browser.browsingContext?.currentWindowGlobal?.contentParentId;
    if (contentParentId !== undefined) {
      options.contentParentId = contentParentId;
    }

    return this.FaviconUtils.getMozRemoteImageURL(aUrl, options);
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
      lazy.PlacesUtils.history.update(pageInfo).catch(console.error);
    }
    if (tab) {
      tab.description = aDescription;
    }
  }

  #cachedTitleInfo = null;
  #populateTitleCache() {
    this.#cachedTitleInfo = {};
    for (let id of [
      "mainWindowTitle",
      "privateWindowTitle",
      "privateWindowSuffixForContent",
    ]) {
      this.#cachedTitleInfo[id] =
        this.document.getElementById(id)?.textContent || "";
    }
  }

  /**
   * The current Taskbar Tab associated with this window. This cannot
   * change after it is first set.
   *
   * @type {TaskbarTab|null}
   */
  #taskbarTab = null;

  /**
   * The last title associated with this window, avoiding re-lookup
   * of the container name and localizations.
   *
   * @type {string|null}
   */
  #taskbarTabTitle = null;

  /**
   * The last profile used when determining the Taskbar Tab title. (This
   * can change, for example if the first profile is made after opening
   * the Taskbar Tab.)
   *
   * @type {string|null}
   */
  #taskbarTabTitleLastProfile = null;

  /**
   * Determines the content of the window title that relates to the Taskbar
   * Tab. This includes the name of the Taskbar Tab, of the container, and
   * of the profile.
   *
   * If no Taskbar Tab is in use, the profile is added by
   * getWindowTitleForBrowser and this returns null.
   *
   * @param {string} [aProfile]
   *   Name of the current profile, if it should appear in the title.
   * @returns {string|null} The part of the title that was determined from
   * the Taskbar Tab, or null if nothing is needed.
   */
  #determineTaskbarTabTitle(aProfile) {
    if (!this._shouldExposeContentTitle) {
      // The Taskbar Tab and container info expose what site the user's on.
      return null;
    }

    if (this.#taskbarTabTitle && this.#taskbarTabTitleLastProfile == aProfile) {
      return this.#taskbarTabTitle;
    }

    let id = this.TaskbarTabsUtils.getTaskbarTabIdFromWindow(
      this.documentGlobal
    );
    if (!id) {
      return null;
    }

    if (!this.#taskbarTab) {
      this.TaskbarTabs.getTaskbarTab(id)
        .then(tt => {
          this.#taskbarTab = tt;
          this.updateTitlebar();
        })
        .catch(() => {
          // The taskbar tab doesn't exist; leave it as-is.
        });
      return null;
    }

    let containerLabel = this.#taskbarTab.userContextId
      ? lazy.ContextualIdentityService.getUserContextLabel(
          this.#taskbarTab.userContextId
        )
      : "";

    let stringName = "taskbar-tab-title-default";
    if (containerLabel && aProfile) {
      stringName = "taskbar-tab-title-container-profile";
    } else if (containerLabel && !aProfile) {
      stringName = "taskbar-tab-title-container";
    } else if (!containerLabel && aProfile) {
      stringName = "taskbar-tab-title-profile";
    }

    this.#taskbarTabTitle = this.tabLocalization.formatValueSync(stringName, {
      name: this.#taskbarTab.name,
      container: containerLabel,
      profile: aProfile,
    });
    return this.#taskbarTabTitle;
  }

  #determineContentTitle(browser) {
    let title = "";
    if (
      !this._shouldExposeContentTitle ||
      (lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal) &&
        !this._shouldExposeContentTitlePbm)
    ) {
      return title;
    }

    let docElement = this.document.documentElement;
    // If location bar is hidden and the URL type supports a host,
    // add the scheme and host to the title to prevent spoofing.
    // XXX https://bugzilla.mozilla.org/show_bug.cgi?id=22183#c239
    try {
      if (
        docElement.hasAttribute("web-extension-popup-window") ||
        docElement.hasAttribute("chromeless-window")
      ) {
        const uri = Services.io.createExposableURI(browser.currentURI);
        let prefix = uri.prePath;
        if (uri.scheme == "about") {
          prefix = uri.spec;
        } else if (uri.scheme == "moz-extension") {
          const ext = WebExtensionPolicy.getByHostname(uri.host);
          if (ext && ext.name) {
            let extensionLabel = this.document.getElementById(
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

    let tab = this.getTabForBrowser(browser);
    if (tab._labelIsContentTitle) {
      // Strip out any null bytes in the content title, since the
      // underlying widget implementations of nsWindow::SetTitle pass
      // null-terminated strings to system APIs.
      title += tab.getAttribute("label").replace(/\0/g, "");
    }
    return title;
  }

  getWindowTitleForBrowser(browser) {
    if (!this.#cachedTitleInfo) {
      this.#populateTitleCache();
    }
    let contentTitle = this.#determineContentTitle(browser);
    let docElement = this.document.documentElement;
    let isTemporaryPrivateWindow =
      docElement.getAttribute("privatebrowsingmode") == "temporary";

    let profileIdentifier =
      lazy.SelectableProfileService?.isEnabled &&
      lazy.SelectableProfileService.getCachedProfileCount() > 1 &&
      lazy.SelectableProfileService.currentProfile?.name.replace(/\0/g, "");
    // Note that empty/falsy bits get filtered below.

    let taskbarTabTitle = this.#determineTaskbarTabTitle(profileIdentifier);
    let parts = [contentTitle, taskbarTabTitle ?? profileIdentifier];

    // On macOS PB windows, add the private window suffix if we have a content
    // title. We'll add the brand name and private window suffix for all other
    // platforms below.
    if (
      AppConstants.platform == "macosx" &&
      contentTitle &&
      isTemporaryPrivateWindow
    ) {
      parts.push(this.#cachedTitleInfo.privateWindowSuffixForContent);
    }

    // Show the brand name if we aren't a Taskbar Tab, since that is done in
    // #determineTaskbarTabTitle. On macOS we only do this if we don't have a
    // content title; elsewhere, the brand becomes a suffix in the title bar.
    if (
      !taskbarTabTitle &&
      (!contentTitle || AppConstants.platform != "macosx")
    ) {
      parts.push(
        this.#cachedTitleInfo[
          isTemporaryPrivateWindow ? "privateWindowTitle" : "mainWindowTitle"
        ]
      );
    }

    return parts.filter(p => !!p).join(" — ");
  }

  updateTitlebar() {
    this.document.title = this.getWindowTitleForBrowser(this.selectedBrowser);
  }

  updateCurrentBrowser(aForceUpdate) {
    let newBrowser = this.getBrowserAtIndex(this.tabContainer.selectedIndex);
    if (this.selectedBrowser == newBrowser && !aForceUpdate) {
      return;
    }

    let oldBrowser = this.selectedBrowser;
    // Once the async switcher starts, it's unpredictable when it will touch
    // the address bar, thus we store its state immediately.
    this.documentGlobal.gURLBar?.saveSelectionStateForBrowser(oldBrowser);

    let newTab = this.getTabForBrowser(newBrowser);

    let timerId;
    if (!aForceUpdate) {
      timerId = Glean.browserTabswitch.update.start();

      if (this.documentGlobal.gMultiProcessBrowser) {
        this._asyncTabSwitching = true;
        this._getSwitcher().requestTab(newTab);
        this._asyncTabSwitching = false;
      }

      this.document.commandDispatcher.lock();
    }

    let oldTab = this.selectedTab;

    // Preview mode should not reset the owner
    if (!this.#previewMode && !oldTab.selected) {
      oldTab.owner = null;
    }

    let lastRelatedTab = this.#lastRelatedTabMap.get(oldTab);
    if (lastRelatedTab) {
      if (!lastRelatedTab.selected) {
        lastRelatedTab.owner = null;
      }
    }
    this.#lastRelatedTabMap = new WeakMap();

    if (!this.documentGlobal.gMultiProcessBrowser) {
      oldBrowser.removeAttribute("primary");
      oldBrowser.docShellIsActive = false;
      newBrowser.setAttribute("primary", "true");
      newBrowser.docShellIsActive = !this.document.hidden;
    }

    this._selectedBrowser = newBrowser;
    this._selectedTab = newTab;
    this.showTab(newTab);

    this.appendStatusPanel();

    this._updateVisibleNotificationBox(newBrowser);

    let oldBrowserPopupsBlocked =
      oldBrowser.popupAndRedirectBlocker.getBlockedPopupCount();
    let newBrowserPopupsBlocked =
      newBrowser.popupAndRedirectBlocker.getBlockedPopupCount();
    if (oldBrowserPopupsBlocked != newBrowserPopupsBlocked) {
      newBrowser.popupAndRedirectBlocker.sendObserverUpdateBlockedPopupsEvent();
    }

    let oldBrowserRedirectBlocked =
      oldBrowser.popupAndRedirectBlocker.isRedirectBlocked();
    let newBrowserRedirectBlocked =
      newBrowser.popupAndRedirectBlocker.isRedirectBlocked();
    if (oldBrowserRedirectBlocked != newBrowserRedirectBlocked) {
      newBrowser.popupAndRedirectBlocker.sendObserverUpdateBlockedRedirectEvent();
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

    let listener = this.#tabListeners.get(newTab);
    if (listener && listener._stateFlags) {
      this._callProgressListeners(
        null,
        "onUpdateCurrentBrowser",
        [
          listener._stateFlags,
          listener._status,
          listener._message,
          listener._totalProgress,
        ],
        true,
        false
      );
    }

    if (!this.#previewMode) {
      newTab.recordTimeFromUnloadToReload();
      newTab.updateLastAccessed();
      oldTab.updateLastAccessed();
      // if this is the foreground window, update the last-seen timestamps.
      if (this.documentGlobal == lazy.BrowserWindowTracker.getTopWindow()) {
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
    if (!this.#previewMode) {
      // We've selected the new tab, so go ahead and notify listeners.
      let event = new this.documentGlobal.CustomEvent("TabSelect", {
        bubbles: true,
        cancelable: false,
        detail: {
          previousTab: oldTab,
        },
      });
      newTab.dispatchEvent(event);

      // Check if switched repeatedly between tabs in the last minute
      this._checkIfShouldTriggerTabSelectMessage(oldTab, newTab);

      this._tabAttrModified(oldTab, ["selected"]);
      this._tabAttrModified(newTab, ["selected"]);

      this._startMultiSelectChange();
      this.#multiSelectChangeSelected = true;
      this.clearMultiSelectedTabs();
      if (this.#multiSelectChangeAdditions.size) {
        // Some tab has been multiselected just before switching tabs.
        // The tab that was selected at that point should also be multiselected.
        this.addToMultiSelectedTabs(oldTab);
      }

      if (!this.documentGlobal.gMultiProcessBrowser) {
        this._adjustFocusBeforeTabSwitch(oldTab, newTab);
        this._adjustFocusAfterTabSwitch(newTab);
      }

      // Bug 1781806 - A forced update can indicate the tab was already
      // selected. To ensure the internal state of the Urlbar is kept in
      // sync, notify it as if focus changed. Alternatively, if there is no
      // force update but the load context is not using remote tabs, there
      // can be a focus change due to the _adjustFocus above.
      if (aForceUpdate || !this.documentGlobal.gMultiProcessBrowser) {
        this.documentGlobal.gURLBar.afterTabSwitchFocusChange();
      }
    }

    this.#updateUserContextUIIndicator();
    this.documentGlobal.gPermissionPanel.updateSharingIndicator();

    // Enable touch events to start a native dragging
    // session to allow the user to easily drag the selected tab.
    // This is currently only supported on Windows.
    oldTab.removeAttribute("touchdownstartsdrag");
    newTab.setAttribute("touchdownstartsdrag", "true");

    if (!this.documentGlobal.gMultiProcessBrowser) {
      this.document.commandDispatcher.unlock();

      let event = new this.documentGlobal.CustomEvent("TabSwitchDone", {
        bubbles: true,
        cancelable: true,
      });
      this.dispatchEvent(event);
    }

    if (!aForceUpdate) {
      Glean.browserTabswitch.update.stopAndAccumulate(timerId);
    }
  }

  /**
   * Tracks tab switching behavior and triggers a message when a user
   * repeatedly switches between the same two tabs.
   *
   * @param {object} oldTab - The tab being switched away from.
   * @param {object} newTab - The tab being switched to.
   */
  async _checkIfShouldTriggerTabSelectMessage(oldTab, newTab) {
    const ONE_MINUTE_MS = 60000;
    const LIMIT_FOR_TRIGGER = 3;
    const now = Date.now();

    const oldTabSpec = oldTab.linkedBrowser.currentURI.spec;
    const newTabSpec = newTab.linkedBrowser.currentURI.spec;

    if (
      [oldTab, newTab].some(
        tab => tab.linkedBrowser.currentURI.scheme === "about"
      )
    ) {
      return;
    }

    // Only look at entries from the last minute
    this.#tabSelectTimestamps = this.#tabSelectTimestamps.filter(
      entry => now - entry.timestamp < ONE_MINUTE_MS
    );

    const sortedUris = [oldTabSpec, newTabSpec].sort();
    const existingEntry = this.#tabSelectTimestamps.find(
      entry =>
        entry.uris[0] === sortedUris[0] && entry.uris[1] === sortedUris[1]
    );

    if (existingEntry) {
      existingEntry.count++;
      existingEntry.timestamp = now;
      if (existingEntry.count === LIMIT_FOR_TRIGGER) {
        await this.ASRouter.waitForInitialized;
        this.ASRouter.sendTriggerMessage({
          browser: newTab.linkedBrowser,
          id: "tabSwitch",
          context: {
            currentTabsOpen: this.visibleTabs.length,
          },
        });
        this.#tabSelectTimestamps = this.#tabSelectTimestamps.filter(
          entry => entry !== existingEntry
        );
      }
    } else {
      this.#tabSelectTimestamps.push({
        timestamp: now,
        uris: sortedUris,
        count: 1,
      });
    }
  }

  _adjustFocusBeforeTabSwitch(oldTab, newTab) {
    if (this.#previewMode) {
      return;
    }

    let oldBrowser = oldTab.linkedBrowser;
    let newBrowser = newTab.linkedBrowser;

    this.documentGlobal.gURLBar.getBrowserState(oldBrowser).urlbarFocused =
      this.documentGlobal.gURLBar.focused;

    if (this._asyncTabSwitching) {
      newBrowser._userTypedValueAtBeforeTabSwitch = newBrowser.userTypedValue;
    }

    if (this.isFindBarInitialized(oldTab)) {
      let findBar = this.getCachedFindBar(oldTab);
      oldTab._findBarFocused =
        !findBar.hidden && findBar._findField.getAttribute("focused") == "true";
    }

    let activeEl = this.document.activeElement;
    // If focus is on the old tab, move it to the new tab.
    if (activeEl == oldTab) {
      newTab.focus();
    } else if (
      this.documentGlobal.gMultiProcessBrowser &&
      activeEl != newBrowser &&
      activeEl != newTab
    ) {
      // In e10s, if focus isn't already in the tabstrip or on the new browser,
      // and the new browser's previous focus wasn't in the url bar but focus is
      // there now, we need to adjust focus further.
      let keepFocusOnUrlBar =
        newBrowser &&
        this.documentGlobal.gURLBar.getBrowserState(newBrowser).urlbarFocused &&
        this.documentGlobal.gURLBar.focused;
      if (!keepFocusOnUrlBar) {
        // Clear focus so that _adjustFocusAfterTabSwitch can detect if
        // some element has been focused and respect that.
        this.document.activeElement.blur();
      }
    }
  }

  _adjustFocusAfterTabSwitch(newTab) {
    // Don't steal focus from the tab bar.
    if (this.document.activeElement == newTab) {
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
    if (this.documentGlobal.gURLBar.getBrowserState(newBrowser).urlbarFocused) {
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
          const currentActiveElement = this.document.activeElement;
          this.documentGlobal.gURLBar.inputField.addEventListener(
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

              if (currentActiveElement != this.document.activeElement) {
                return;
              }
              this.documentGlobal.gURLBar.restoreSelectionStateForBrowser(
                newBrowser
              );
            },
            { once: true }
          );
        } else {
          this.documentGlobal.gURLBar.restoreSelectionStateForBrowser(
            newBrowser
          );
        }
      };

      // This inDOMFullscreen attribute indicates that the page has something
      // such as a video in fullscreen mode. Opening a new tab will cancel
      // fullscreen mode, so we need to wait for that to happen and then
      // select the url field.
      if (this.document.documentElement.hasAttribute("inDOMFullscreen")) {
        this.documentGlobal.addEventListener(
          "MozDOMFullscreen:Exited",
          selectURL,
          {
            once: true,
            wantsUntrusted: false,
          }
        );
        return;
      }

      if (!this.documentGlobal.fullScreen || newTab.isEmpty) {
        selectURL();
        return;
      }
    }

    // Focus the find bar if it was previously focused for that tab.
    if (
      this.documentGlobal.gFindBarInitialized &&
      !this.documentGlobal.gFindBar.hidden &&
      this.selectedTab._findBarFocused
    ) {
      this.documentGlobal.gFindBar._findField.focus();
      return;
    }

    // Don't focus the content area if something has been focused after the
    // tab switch was initiated.
    if (
      this.documentGlobal.gMultiProcessBrowser &&
      this.document.activeElement != this.document.body
    ) {
      return;
    }

    // We're now committed to focusing the content area.
    let fm = Services.focus;
    let focusFlags = fm.FLAG_NOSCROLL;

    if (!this.documentGlobal.gMultiProcessBrowser) {
      let newFocusedElement = fm.getFocusedElementForWindow(
        this.documentGlobal.content,
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

    let event = new this.documentGlobal.CustomEvent("TabAttrModified", {
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
    aBrowser._sharingState = aBrowser._sharingState?.webRTC
      ? { webRTC: {} }
      : {};
    tab.removeAttribute("sharing");
    this._tabAttrModified(tab, ["sharing"]);
    if (aBrowser == this.selectedBrowser) {
      this.documentGlobal.gPermissionPanel.updateSharingIndicator();
    }
  }

  updateBrowserSharing(aBrowser, aState) {
    let tab = this.getTabForBrowser(aBrowser);
    if (!tab) {
      return;
    }

    if (aBrowser._sharingState == null) {
      aBrowser._sharingState = {};
    }
    aBrowser._sharingState = Object.assign(aBrowser._sharingState, aState);

    if ("webRTC" in aState) {
      if (aBrowser._sharingState.webRTC?.sharing) {
        if (aBrowser._sharingState.webRTC.paused) {
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
      this.documentGlobal.gPermissionPanel.updateSharingIndicator();
    }
  }

  getTabSharingState(aTab) {
    // Normalize the state object for consumers (ie.extensions).
    let browser = aTab.linkedBrowser;
    let state = Object.assign(
      {},
      browser._sharingState && browser._sharingState.webRTC
    );
    return {
      camera: !!state.camera,
      microphone: !!state.microphone,
      screen: state.screen && state.screen.replace("Paused", ""),
    };
  }

  setInitialTabTitle(aTab, aTitle, aOptions = {}) {
    // Convert some non-content title (actually a url) to human readable title
    if (
      !aOptions.isContentTitle &&
      this.documentGlobal.isBlankPageURL(aTitle)
    ) {
      aTitle = this.tabContainer.emptyTabTitle;
    }

    if (aTitle) {
      if (!aTab.getAttribute("label")) {
        aTab._labelIsInitialTitle = true;
      }

      this._setTabLabel(aTab, aTitle, aOptions);
    }
  }

  #dataURLRegEx = /^data:[^,]+;base64,/i;

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
  #nonPrintingRegEx =
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
    if (this.#nonPrintingRegEx.test(title)) {
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

      if (title && !this.documentGlobal.isBlankPageURL(title)) {
        isURL = true;
        if (title.length <= 500 || !this.#dataURLRegEx.test(title)) {
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
    if (!aLabel || (isURL && /^about:reader\?url=/.test(aLabel))) {
      return false;
    }

    // If it's a long data: URI that uses base64 encoding, truncate to a
    // reasonable length rather than trying to display the entire thing,
    // which can hang or crash the browser.
    // We can't shorten arbitrary URIs like this, as bidi etc might mean
    // we need the trailing characters for display. But a base64-encoded
    // data-URI is plain ASCII, so this is OK for tab-title display.
    // (See bug 1408854.)
    if (isURL && aLabel.length > 500 && this.#dataURLRegEx.test(aLabel)) {
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

    if (aLabel.length > TAB_LABEL_MAX_LENGTH) {
      // Clamp overly-long titles to avoid DoS-type hangs (bug 736194).
      aLabel = aLabel.substring(0, TAB_LABEL_MAX_LENGTH);
    }

    aTab._labelIsContentTitle = isContentTitle;

    if (aTab.getAttribute("label") == aLabel) {
      return false;
    }

    let dwu = this.documentGlobal.windowUtils;
    let isRTL =
      dwu.getDirectionFromText(aLabel) == Ci.nsIDOMWindowUtils.DIRECTION_RTL;

    aTab.setAttribute("label", aLabel);
    aTab.setAttribute("labeldirection", isRTL ? "rtl" : "ltr");
    aTab.toggleAttribute(
      "labelendaligned",
      isRTL != (this.document.dir == "rtl")
    );

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

  /**
   * Load multiple URIs in tabs.
   *
   * @param {string[]} aURIs
   *   Array of URIs to load.
   * @param {object} [options]
   * @param {boolean} [options.allowInheritPrincipal]
   *   Allow the loads to inherit the triggering principal.
   * @param {boolean} [options.allowThirdPartyFixup]
   *   Allow transforming a URI into a search query.
   * @param {boolean} [options.inBackground]
   *   Leave the current tab selected instead of switching to the first tab
   *   that was opened.
   * @param {number} [options.newIndex]
   *   The position to open the first tab at, expressed as an index within the
   *   `tabs` array. The remaining tabs follow it.
   * @param {number} [options.elementIndex]
   *   The same position, expressed as an index within the
   *   `MozTabbrowserTabs::dragAndDropElements` array.
   * @param {nsIInputStream[]} [options.postDatas]
   *   Data to post as part of each request, in the same order as `aURIs`.
   * @param {boolean} [options.replace]
   *   Load the first URI in an existing tab rather than opening a tab for it.
   * @param {MozTabbrowserTabGroup} [options.tabGroup]
   *   A tab group to open the tabs in.
   * @param {MozTabbrowserTab} [options.targetTab]
   *   The tab to load the first URI in when `replace` is set. Defaults to the
   *   selected tab.
   * @param {nsIPrincipal} [options.triggeringPrincipal]
   *   Triggering principal to pass to docshell for the loads.
   * @param {nsIPolicyContainer} [options.policyContainer]
   *   The policy container that should apply to the loads.
   * @param {number} [options.userContextId]
   *   The userContextId (container identifier) to open the tabs in.
   * @param {boolean} [options.fromExternal]
   *   Whether the URIs were supplied to Firefox from an external application.
   * @param {boolean} [options.newWindowLoad]
   *   Whether the load initiates a new window, which is not web-controlled.
   *   Only honoured together with `replace`.
   * @returns {MozTabbrowserTab[]}
   *   Array of tabs that were opened or reused, in the same order as aURIs.
   */
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
      policyContainer,
      userContextId,
      fromExternal,
      newWindowLoad,
    } = {}
  ) {
    if (!aURIs.length) {
      return [];
    }

    let tabs = [];

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
      if (newWindowLoad) {
        browser.initiatedFromNonWebControlled = true;
      }
      try {
        browser.fixupAndLoadURIString(aURIs[0], {
          loadFlags,
          postData: postDatas && postDatas[0],
          triggeringPrincipal,
          policyContainer,
        });
      } catch (e) {
        // Ignore failure in case a URI is wrong, so we can continue
        // opening the next ones.
      }
      tabs.push(targetTab || this.selectedTab);
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
        policyContainer,
        fromExternal,
        tabGroup,
      };
      if (newIndex > -1) {
        params.tabIndex = newIndex;
      }
      firstTabAdded = this.addTab(aURIs[0], params);
      tabs.push(firstTabAdded);
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
        policyContainer,
        fromExternal,
        tabGroup,
      };
      if (targetTabIndex > -1) {
        params.tabIndex = ++tabNum;
      }
      let tab = this.addTab(aURIs[i], params);
      tabs.push(tab);
    }

    if (firstTabAdded && !inBackground) {
      this.selectedTab = firstTabAdded;
    }

    return tabs;
  }

  updateBrowserRemoteness(aBrowser, { newFrameloader, remoteType } = {}) {
    let isRemote = aBrowser.hasAttribute("remote");

    // We have to be careful with this here, as the "no remote type" is null,
    // not a string. Make sure to check only for undefined, since null is
    // allowed.
    if (remoteType === undefined) {
      throw new Error("Remote type must be set!");
    }

    let shouldBeRemote = remoteType !== lazy.E10SUtils.NOT_REMOTE;

    if (!this.documentGlobal.gMultiProcessBrowser && shouldBeRemote) {
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

    let evt = this.document.createEvent("Events");
    evt.initEvent("BeforeTabRemotenessChange", true, false);
    tab.dispatchEvent(evt);

    // Unhook our progress listener.
    let filter = this.#tabFilters.get(tab);
    let listener = this.#tabListeners.get(tab);
    // We should always have a filter, but if we fail to create a content
    // process when creating a new tab, we can end up here trying to switch
    // remoteness to load about:tabcrashed, without a filter/listener.
    if (filter) {
      aBrowser.webProgress.removeProgressListener(filter);
      filter.removeProgressListener(listener);
    }

    // We'll be creating a new listener, so destroy the old one.
    listener?.destroy();

    let oldUserTypedValue = aBrowser.userTypedValue;
    let hadStartedLoad = aBrowser.didStartLoadSinceLastUserTyping();

    // Change the "remote" attribute.

    // Make sure the browser is destroyed so it unregisters from observer notifications
    aBrowser.destroy();

    if (shouldBeRemote) {
      aBrowser.setAttribute("remote", "true");
      aBrowser.setAttribute("remoteType", remoteType);
    } else {
      aBrowser.removeAttribute("remote");
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
    this.#tabListeners.set(tab, listener);
    if (!filter) {
      filter = Cc[
        "@mozilla.org/appshell/component/browser-status-filter;1"
      ].createInstance(Ci.nsIWebProgress);
      this.#tabFilters.set(tab, filter);
    }
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

    evt = this.document.createEvent("Events");
    evt.initEvent("TabRemotenessChange", true, false);
    tab.dispatchEvent(evt);

    return true;
  }

  updateBrowserRemotenessByURL(aBrowser, aURL, aOptions = {}) {
    if (!this.documentGlobal.gMultiProcessBrowser) {
      return this.updateBrowserRemoteness(aBrowser, {
        remoteType: lazy.E10SUtils.NOT_REMOTE,
      });
    }

    let oldRemoteType = aBrowser.remoteType;

    aOptions.remoteType = ChromeUtils.predictRemoteTypeForURI(aURL, {
      window: this.documentGlobal,
      userContextId: aBrowser.getAttribute("usercontextid") ?? 0,
      preferredRemoteType: oldRemoteType,
    });

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
    let b = this.document.createXULElement("browser");
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

    if (this.documentGlobal.gMultiProcessBrowser || remoteType) {
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

    if (
      lazy.AIWindow.isAIWindowActive(this.documentGlobal) ||
      this._allowTransparentBrowser
    ) {
      b.setAttribute("transparent", "true");
    }

    Services.obs.notifyObservers(
      b,
      "tabbrowser-browser-element-will-be-inserted"
    );

    let stack = this.document.createXULElement("stack");
    stack.className = "browserStack";
    stack.appendChild(b);

    let browserContainer = this.document.createXULElement("box");
    browserContainer.className = "browserContainer";
    browserContainer.appendChild(stack);

    let browserSidebarContainer = this.document.createXULElement("hbox");
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

    let names = this.#browserBindingProperties;

    for (let i = 0; i < names.length; i++) {
      let name = names[i];
      let getter;
      let setter;
      switch (name) {
        case "audioMuted":
          getter = () => aTab.hasAttribute("muted");
          break;
        case "contentTitle":
          getter = () => lazy.SessionStore.getLazyTabValue(aTab, "title");
          break;
        case "currentURI":
          getter = () => {
            // Avoid recreating the same nsIURI object over and over again...
            if (browser._cachedCurrentURI) {
              return browser._cachedCurrentURI;
            }
            let url =
              lazy.SessionStore.getLazyTabValue(aTab, "url") || "about:blank";
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
          getter = () => browser.hasAttribute("remote");
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
            this._insertBrowser(aTab);
          };
          break;
        case "remoteType":
          getter = () => {
            let url =
              lazy.SessionStore.getLazyTabValue(aTab, "url") || "about:blank";
            return ChromeUtils.predictRemoteTypeForURI(url, {
              window: this.documentGlobal,
              userContextId: aTab.getAttribute("usercontextid"),
            });
          };
          break;
        case "userTypedValue":
        case "userTypedClear":
          getter = () => lazy.SessionStore.getLazyTabValue(aTab, name);
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
    // If browser is already inserted or window is closed don't do anything.
    if (aTab.linkedPanel || this.documentGlobal.closed) {
      return;
    }

    let browser = aTab.linkedBrowser;

    // If browser is a lazy browser, delete the substitute properties.
    if (this.#browserBindingProperties[0] in browser) {
      for (let name of this.#browserBindingProperties) {
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
      // AppendChild will cause a synchronous about:blank load.
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
    this.#tabListeners.set(aTab, tabListener);
    this.#tabFilters.set(aTab, filter);

    browser.droppedLinkHandler = this._defaultDropLinkHandler;
    browser.loadURI = URILoadingWrapper.loadURI.bind(
      URILoadingWrapper,
      browser
    );
    browser.fixupAndLoadURIString =
      URILoadingWrapper.fixupAndLoadURIString.bind(URILoadingWrapper, browser);

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
      this.#updateUserContextUIIndicator();
    }

    // Only fire this event if the tab is already in the DOM
    // and will be handled by a listener.
    if (aTab.isConnected) {
      var evt = new this.documentGlobal.CustomEvent("TabBrowserInserted", {
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
      this.#windowIsClosing ||
      !browser.isConnected ||
      !browser.isRemoteBrowser ||
      !browser.permitUnload(action).permitUnload
    ) {
      return false;
    }

    // discarding a browser will dismiss any dialogs, so don't
    // allow this unless we're forcing it.
    if (
      !aForceDiscard &&
      this.getTabDialogBox(browser)._tabDialogManager._dialogs.length
    ) {
      return false;
    }

    return true;
  }

  async prepareDiscardBrowser(aTab) {
    let browser = aTab.linkedBrowser;
    // This is similar to the checks in _mayDiscardBrowser, but
    // doesn't have to be complete (and we want to be sure not to
    // fire the beforeunload event). Calling TabStateFlusher.flush()
    // and then not unloading the browser is fine.
    if (aTab.closing || this.#windowIsClosing || !browser.isRemoteBrowser) {
      return;
    }

    // Flush the tab's state so session restore has the latest data.
    await this.TabStateFlusher.flush(browser);
  }

  discardBrowser(aTab, aForceDiscard) {
    let browser = aTab.linkedBrowser;

    if (!this._mayDiscardBrowser(aTab, aForceDiscard)) {
      return false;
    }

    // Reset sharing state.
    this.resetBrowserSharing(browser);
    lazy.webrtcUI.forgetStreamsFromBrowserContext(browser.browsingContext);

    // Abort any dialogs since the browser is about to be discarded.
    let tabDialogBox = this.getTabDialogBox(browser);
    tabDialogBox.abortAllDialogs();

    // Set browser parameters for when browser is restored.  Also remove
    // listeners and set up lazy restore data in SessionStore. This must
    // be done before browser is destroyed and removed from the document.
    aTab._browserParams = {
      uriIsAboutBlank: browser.currentURI.spec == "about:blank",
      remoteType: browser.remoteType,
      usingPreloadedContent: false,
    };

    lazy.SessionStore.resetBrowserToLazyState(aTab);
    // Indicate that this tab was explicitly unloaded (i.e. not
    // from a session restore) in case we want to style that
    // differently.
    if (aForceDiscard) {
      aTab.toggleAttribute("discarded", true);
    }

    // Remove the tab's filter and progress listener.
    let filter = this.#tabFilters.get(aTab);
    let listener = this.#tabListeners.get(aTab);
    browser.webProgress.removeProgressListener(filter);
    filter.removeProgressListener(listener);
    listener.destroy();

    this.#tabListeners.delete(aTab);
    this.#tabFilters.delete(aTab);

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

    let evt = new this.documentGlobal.CustomEvent("TabBrowserDiscarded", {
      bubbles: true,
    });
    aTab.dispatchEvent(evt);
    return true;
  }

  /**
   * Loads a tab with a default null principal unless specified
   *
   * @param {string} aURI
   * @param {object} [params]
   *   Options from `Tabbrowser.addTab`.
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

  /**
   * @param {MozTabbrowserTab} tab
   * @returns {void}
   *   New tab will be the `Tabbrowser.selectedTab` or the subject of a
   *   notification on the `browser-open-newtab-start` topic.
   */
  addAdjacentNewTab(tab) {
    Services.obs.notifyObservers(
      {
        wrappedJSObject: new Promise(resolve => {
          this.selectedTab = this.addTrustedTab(
            this.documentGlobal.BROWSER_NEW_TAB_URL,
            {
              tabIndex: tab._tPos + 1,
              userContextId: tab.userContextId,
              tabGroup: tab.group,
              focusUrlBar: true,
            }
          );
          resolve(this.selectedBrowser);
        }),
      },
      "browser-open-newtab-start"
    );
  }

  /**
   * Creates a tab directly after `tab`.
   *
   * @param {MozTabbrowserTab} adjacentTab
   * @param {string} uriString
   * @param {object} [options]
   *   Options from `Tabbrowser.addTab`.
   */
  addAdjacentTab(adjacentTab, uriString, options = {}) {
    // If the caller opted out of tab group membership but `tab` is in a
    // tab group, insert the tab after `tab`'s group. Otherwise, insert the
    // new tab right after `tab`.
    const tabIndex =
      !options.tabGroup && adjacentTab.group
        ? adjacentTab.group.tabs.at(-1)._tPos + 1
        : adjacentTab._tPos + 1;

    return this.addTab(uriString, {
      ...options,
      tabIndex,
    });
  }

  /**
   * Must only be used sparingly for content that came from Chrome context
   * If in doubt use addWebTab
   *
   * @param {string} aURI
   * @param {object} [options]
   * @see Tabbrowser.addTab
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
   * @param {boolean} [options.allowInheritPrincipal]
   *   Allow the load to inherit the triggering principal.
   * @param {boolean} [options.allowThirdPartyFixup]
   *   Allow transforming the URI into a search query.
   * @param {boolean} [options.bulkOrderedOpen]
   *   Keep the tab in the order it was opened in rather than inserting it
   *   next to the current tab, as wanted when opening a set of tabs at once.
   * @param {string} [options.charset]
   *   Character set to use for the load. Legacy argument - do not use.
   * @param {boolean} [options.createLazyBrowser]
   *   Create the tab with a lazy browser, which only loads its URI once the
   *   tab is first selected.
   * @param {object} [options.eventDetail]
   *   Additional information to include in the `CustomEvent.detail`
   *   of the resulting TabOpen event.
   * @param {boolean} [options.focusUrlBar]
   *   Focus the address bar when the tab is selected.
   * @param {boolean} [options.forceNotRemote]
   *   Create the tab's browser in the parent process.
   * @param {boolean} [options.forceAllowDataURI]
   *   Force allow a data URI to load as a toplevel load.
   * @param {boolean} [options.fromExternal]
   *   Whether this tab was opened from a URL supplied to Firefox from an
   *   external application.
   * @param {boolean} [options.inBackground=true]
   *   Whether the tab is being opened in the background. This does not
   *   select the tab - it only decides whether the selected tab becomes the
   *   new tab's owner, i.e. the tab to select when the new tab is closed.
   * @param {boolean} [options.isCaptivePortalTab]
   *   Whether the tab loads a captive portal login page, whose load must not
   *   use DNS over HTTPS.
   * @param {number} [options.elementIndex]
   *   The position to open the tab at, expressed as an index within the
   *   `MozTabbrowserTabs::dragAndDropElements` array.
   * @param {number} [options.tabIndex]
   *   The same position, expressed as an index within the `tabs` array.
   * @param {string} [options.lazyTabTitle]
   *   Title to give a lazy browser's tab until its URI loads.
   * @param {string} [options.name]
   *   Name of the window the tab's browser represents, as passed to
   *   `window.open()`.
   * @param {boolean} [options.noInitialLabel]
   *   Leave the tab's label empty until the load provides a title, rather
   *   than labelling it with its URI.
   * @param {nsIOpenWindowInfo} [options.openWindowInfo]
   *   Information about the content window that asked for this tab, when it
   *   comes from a `window.open()` call.
   * @param {MozBrowser} [options.openerBrowser]
   *   The browser that started the load, used to position the tab and to
   *   inherit attributes such as the user context ID from.
   * @param {nsIPrincipal} [options.originPrincipal]
   *   Origin principal to inherit when `uriString` is given and no preloaded
   *   browser is used.
   * @param {nsIPrincipal} [options.originStoragePrincipal]
   *   Origin storage principal to inherit when `uriString` is given and no
   *   preloaded browser is used.
   * @param {MozTabbrowserTab} [options.ownerTab]
   *   The tab to select when this tab is closed. Defaults to the selected
   *   tab for a foreground tab, and to none for a background tab.
   * @param {boolean} [options.pinned]
   *   Open the tab pinned.
   * @param {nsIInputStream} [options.postData]
   *   Data to post as part of the request.
   * @param {string} [options.preferredRemoteType]
   *   Remote type to prefer for the tab's browser, when the URI can load in
   *   more than one.
   * @param {nsIReferrerInfo} [options.referrerInfo]
   *   Referrer info for the request.
   * @param {boolean} [options.relatedToCurrent]
   *   Whether the tab is related to the current tab, e.g. because a link in
   *   it was clicked, which lets it open next to it. Defaults to whether
   *   `referrerInfo` carries a referrer.
   * @param {number} [options.initialBrowsingContextGroupId]
   *   Browsing context group to create the tab's browser in, which can
   *   place it in the same process as another window.
   * @param {boolean} [options.skipAnimation]
   *   Skip the tab opening animation.
   * @param {boolean} [options.skipBackgroundNotify]
   *   Don't draw attention to the tab when it wants to be selected while
   *   loading in the background.
   * @param {MozTabbrowserTabGroup} [options.tabGroup]
   *   A related tab group where this tab should be added, when applicable.
   *   When present, the tab is expected to reside in this tab group. When
   *   absent, the tab is expected to be a standalone tab.
   * @param {nsIPrincipal} options.triggeringPrincipal
   *   Triggering principal to pass to docshell for the load.
   * @param {number} [options.userContextId]
   *   The userContextId (container identifier) to open the tab in.
   *   Inherited from the opener tab when absent.
   * @param {nsIPolicyContainer} [options.policyContainer]
   *   The policy container that should apply to the load.
   * @param {boolean} [options.skipLoad]
   *   Create the tab's browser without starting a load in it. Defaults to
   *   `createLazyBrowser`.
   * @param {boolean} [options.insertTab=true]
   *   Insert the tab into the tab strip.
   * @param {object} [options.globalHistoryOptions]
   *   Used by :doc:`Places </browser/places/index>` to keep track of search
   *   related metadata for the load.
   * @param {string} [options.triggeringRemoteType]
   *   The remoteType triggering this load.
   * @param {nsILoadInfo_SchemelessInputType} [options.schemelessInput]
   *   Whether the search/URL term was without an explicit scheme.
   * @param {boolean} [options.hasValidUserGestureActivation=false]
   *   Indicates if a valid user gesture caused this load. This informs
   *   e.g. popup blocker decisions.
   * @param {boolean} [options.textDirectiveUserActivation=false]
   *   Whether a user gesture allows the load to scroll to a text fragment.
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
      eventDetail,
      focusUrlBar,
      forceNotRemote,
      forceAllowDataURI,
      fromExternal,
      inBackground = true,
      isCaptivePortalTab,
      elementIndex,
      tabIndex,
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
      policyContainer,
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

    if (!UserInteraction.running("browser.tabs.opening", this.documentGlobal)) {
      UserInteraction.start(
        "browser.tabs.opening",
        "initting",
        this.documentGlobal
      );
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
      !this.documentGlobal.gReduceMotion;

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
        // Insert the tab into the tab container in the correct position.
        this.#insertTabAtIndex(t, {
          elementIndex,
          tabIndex,
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
        this.documentGlobal.gURLBar.getBrowserState(b).urlbarFocused = true;
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
            tabGroup?.id,
            lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
          );
          b.registeredOpenURI = lazyBrowserURI;
        }
        // If we're not inserting the tab into the DOM, we can't set the tab
        // state meaningfully. Session restore (the only caller who does this)
        // will have to do this work itself later, when the tabs have been
        // inserted.
        if (insertTab) {
          lazy.SessionStore.setTabState(t, {
            entries: [
              {
                url: lazyBrowserURI?.spec || "about:blank",
                title: lazyTabTitle,
                triggeringPrincipal_base64:
                  lazy.E10SUtils.serializePrincipal(triggeringPrincipal),
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
        if (openerBrowser?.browsingContext && !openWindowInfo) {
          b.browsingContext.crossGroupOpener = openerBrowser.browsingContext;
        }
      }
    } catch (e) {
      console.error("Failed to create tab");
      console.error(e);
      t?.remove();
      if (t?.linkedBrowser) {
        this.#tabFilters.delete(t);
        this.#tabListeners.delete(t);
        this.getPanel(t.linkedBrowser).remove();
      }
      return null;
    }

    if (insertTab) {
      // Fire a TabOpen event
      const tabOpenDetail = {
        ...eventDetail,
        fromExternal,
      };
      this._fireTabOpen(t, tabOpenDetail);

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
        forceAllowDataURI,
        isCaptivePortalTab,
        skipLoad: skipLoad || uriIsAboutBlank,
        referrerInfo,
        charset,
        postData,
        policyContainer,
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
        this.documentGlobal.requestAnimationFrame(() => {
          this.documentGlobal.requestAnimationFrame(() => {
            t.setAttribute("fadein", "true");
          });
        });
      } else {
        this.documentGlobal.requestAnimationFrame(() => {
          t.setAttribute("fadein", "true");
        });
      }
    }

    // Additionally send pinned tab events
    if (pinned) {
      this.#notifyPinnedStatus(t);
    }

    this.documentGlobal.gSharedTabWarning.tabAdded(t);

    if (!inBackground) {
      this.selectedTab = t;
    }
    return t;
  }

  #elementIndexToTabIndex(elementIndex) {
    if (elementIndex < 0) {
      return -1;
    }
    if (elementIndex >= this.tabContainer.dragAndDropElements.length) {
      return this.tabs.length;
    }
    let element = this.tabContainer.dragAndDropElements[elementIndex];
    if (this.isTabGroupLabel(element)) {
      element = element.group.tabs[0];
    }
    if (this.isSplitViewWrapper(element)) {
      element = element.tabs[0];
    }
    return element._tPos;
  }

  /**
   * @param {number} id
   * @returns {MozTabSplitViewWrapper}
   */
  _createTabSplitView(id) {
    if (id && typeof id !== "number") {
      throw new Error("Unexpected id type: " + typeof id);
    }
    if (!id) {
      id = lazy.SessionStore.getNextSplitViewId();
    }
    let splitview = this.document.createXULElement("tab-split-view-wrapper", {
      is: "tab-split-view-wrapper",
    });
    splitview.splitViewId = id;
    return splitview;
  }

  /**
   * Adds a new tab split view.
   *
   * @param {object[]} tabs
   *   The set of tabs to include in the split view.
   * @param {object} [options]
   * @param {number} [options.id]
   *   Optionally assign an ID to the split view. Useful when rebuilding an
   *   existing splitview e.g. when restoring. An integer id from a incrementing
   *   counter will be generated if not set.
   * @param {MozTabbrowserTab} [options.insertBefore]
   *   An optional argument that accepts a single tab, which, if passed, will
   *   cause the split view to be inserted just before this tab in the tab strip. By
   *   default, the split view will be created at the end of the tab strip.
   * @param {string} [options.trigger]
   *   The trigger method for creating the split view. Used for telemetry.
   *   Valid values: "menu_add", "menu_open".
   */
  addTabSplitView(
    tabs,
    { id = null, insertBefore = null, trigger = null } = {}
  ) {
    if (!tabs?.length) {
      throw new Error("Cannot create split view with zero tabs");
    }

    // Capture group information before tabs are moved
    let tabGroupInfo = null;
    if (trigger && tabs.length >= 2) {
      const primaryGroup = tabs[0]?.group;
      const secondaryGroup = tabs[1]?.group;
      tabGroupInfo = { primaryGroup, secondaryGroup };
    }

    let splitview = this._createTabSplitView(id);
    this.tabContainer.insertBefore(
      splitview,
      insertBefore?.splitview ?? insertBefore
    );
    splitview.addTabs(tabs);

    // Bail out if the split view is empty at this point. This can happen if all
    // provided tabs are pinned and therefore cannot be grouped.
    if (!splitview.tabs.length) {
      splitview.remove();
      return null;
    }

    // Record telemetry for split view creation
    if (trigger && tabGroupInfo) {
      const tab_layout = this.tabContainer.verticalMode
        ? "vertical"
        : "horizontal";

      let tabgroup;
      const { primaryGroup, secondaryGroup } = tabGroupInfo;

      if (!primaryGroup && !secondaryGroup) {
        tabgroup = "none";
      } else if (primaryGroup && secondaryGroup) {
        tabgroup =
          primaryGroup === secondaryGroup ? "both_same" : "both_different";
      } else if (primaryGroup) {
        tabgroup = "main";
      } else {
        tabgroup = "other";
      }

      Glean.splitview.start.record({
        tab_layout,
        trigger,
        tabgroup,
      });
    }

    this.tabContainer.dispatchEvent(
      new this.documentGlobal.CustomEvent("SplitViewCreated", {
        bubbles: true,
      })
    );
    return splitview;
  }

  /**
   * Show the list of tabs <browsers> that are part of a split view.
   *
   * @param {MozTabbrowserTab[]} tabs
   */
  showSplitViewPanels(tabs) {
    const panels = [];
    for (const tab of tabs) {
      this._insertBrowser(tab);
      this.#insertSplitViewFooter(tab);
      if (tab.linkedBrowser) {
        tab.linkedBrowser.docShellIsActive = true;
        panels.push(tab.linkedPanel);
      }
      const panelEl = this.document.getElementById(tab.linkedPanel);
      panelEl?.classList.toggle("split-view-panel-active", true);
    }
    this.tabpanels.splitViewPanels = panels;
  }

  /**
   * Ensures the split view footer exists for the given tab.
   *
   * @param {MozTabbrowserTab} tab
   */
  #insertSplitViewFooter(tab) {
    const panelEl = this.document.getElementById(tab.linkedPanel);
    if (panelEl?.querySelector("split-view-footer")) {
      return;
    }
    if (panelEl) {
      const footer = this.document.createXULElement("split-view-footer");
      footer.setTab(tab);
      panelEl.querySelector(".browserStack").appendChild(footer);
    }
  }

  openSplitViewMenu(anchorElement) {
    const menu = this.document.getElementById("split-view-menu");
    const isFromFooter =
      anchorElement?.localName === "toolbarbutton" &&
      anchorElement?.parentElement?.localName === "split-view-footer";
    menu.setAttribute("data-trigger-source", isFromFooter ? "footer" : "icon");
    menu.openPopup(anchorElement, "after_start");
  }

  /**
   * @param {string} id
   * @param {string} color
   * @param {boolean} collapsed
   * @param {string} [label]
   * @param {boolean} [isAdoptingGroup=false]
   * @returns {MozTabbrowserTabGroup}
   */
  _createTabGroup(id, color, collapsed, label = "", isAdoptingGroup = false) {
    let group = this.document.createXULElement("tab-group", {
      is: "tab-group",
    });
    group.id = id;
    group.collapsed = collapsed;
    group.color = color;
    group.label = label;
    group.wasCreatedByAdoption = isAdoptingGroup;
    return group;
  }

  /**
   * Adds a new tab group.
   *
   * @param {object[]} tabsAndSplitViews
   *   The set of tabs or split view to include in the group.
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
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes. Defaults to an unknown context.
   */
  addTabGroup(
    tabsAndSplitViews,
    {
      id = null,
      color = null,
      label = "",
      insertBefore = null,
      isAdoptingGroup = false,
      metricsContext = this.TabMetrics.UNKNOWN_CONTEXT,
    } = {}
  ) {
    if (
      !tabsAndSplitViews?.length ||
      tabsAndSplitViews.some(
        tabOrSplitView =>
          !this.isTab(tabOrSplitView) &&
          !this.isSplitViewWrapper(tabOrSplitView)
      )
    ) {
      throw new Error("Cannot create tab group with zero tabs or split views");
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
    let group = this._createTabGroup(id, color, false, label, isAdoptingGroup);
    this.tabContainer.insertBefore(group, insertBefore?.group ?? insertBefore);
    group.addTabs(tabsAndSplitViews, metricsContext);

    // Bail out if the group is empty at this point. This can happen if all
    // provided tabs are pinned and therefore cannot be grouped.
    if (!group.tabs.length) {
      group.remove();
      return null;
    }

    if (metricsContext.isUserTriggered) {
      group.dispatchEvent(
        new this.documentGlobal.CustomEvent("TabGroupCreateByUser", {
          bubbles: true,
          detail: { metricsContext },
        })
      );
    }

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
   */
  async removeTabGroup(group, options = {}) {
    if (this.tabGroupMenu.panel.state != "closed") {
      this.tabGroupMenu.panel.hidePopup(options.animate);
    }

    if (!options.skipPermitUnload) {
      // Process permit unload handlers and allow user cancel
      let cancel = await this.runBeforeUnloadForTabs(group.tabs);
      if (cancel) {
        if (lazy.SessionStore.getSavedTabGroup(group.id)) {
          // If this group is currently saved, it's being removed as part of a
          // save & close operation. We need to forget the saved group
          // if the close is canceled.
          lazy.SessionStore.forgetSavedTabGroup(group.id);
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
      new this.documentGlobal.CustomEvent("TabGroupRemoveRequested", {
        bubbles: true,
        detail: {
          skipSessionStore: options.skipSessionStore,
          metricsContext:
            options.metricsContext ?? this.TabMetrics.UNKNOWN_CONTEXT,
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
   * @param {MozTabbrowserTab} tab The tab to ungroup
   */
  ungroupTab(tab) {
    if (!tab.group) {
      return;
    }

    this.#handleTabMove(tab, () =>
      this.tabContainer.insertBefore(tab, tab.group.nextElementSibling)
    );
  }

  ungroupSplitView(splitView) {
    if (!this.isSplitViewWrapper(splitView)) {
      return;
    }

    this.#handleTabMove(splitView, () =>
      this.tabContainer.insertBefore(
        splitView,
        splitView.tabs[0].group.nextElementSibling
      )
    );
  }

  /**
   * @param {MozTabbrowserTabGroup} group
   * @param {object} [options]
   * @param {number} [options.elementIndex]
   * @param {number} [options.tabIndex]
   * @param {boolean} [options.selectTab]
   * @returns {MozTabbrowserTabGroup}
   */
  adoptTabGroup(group, { elementIndex, tabIndex, selectTab } = {}) {
    if (group.ownerDocument == this.document) {
      return group;
    }
    group.removedByAdoption = true;
    group.saveOnWindowClose = false;

    let oldSelectedTab = selectTab && group.documentGlobal.gBrowser.selectedTab;
    let newTabs = [];
    let adoptedTab;
    let splitview;

    // bug1969925 adopting a tab group will cause the window to close if it
    // is the only thing on the tab strip
    // In this case, the `TabUngrouped` event will not fire, so we have to do it manually
    let noOtherTabsInWindow = group.documentGlobal.gBrowser.nonHiddenTabs.every(
      t => t.group == group
    );

    // We dispatch this event in a separate for loop because the tab extension API
    // expects event.detail to be a tab.
    if (noOtherTabsInWindow) {
      for (let element of group.tabs) {
        group.dispatchEvent(
          new this.documentGlobal.CustomEvent("TabUngrouped", {
            bubbles: true,
            detail: element,
          })
        );
      }
    }

    for (let element of group.tabsAndSplitViews) {
      if (element.tagName == "tab-split-view-wrapper") {
        splitview = this.adoptSplitView(element, {
          elementIndex,
          tabIndex,
        });
        newTabs.push(splitview);
        tabIndex = splitview.tabs[0]._tPos + splitview.tabs.length;
      } else {
        adoptedTab = this.adoptTab(element, {
          elementIndex,
          tabIndex,
          selectTab: element === oldSelectedTab,
        });
        newTabs.push(adoptedTab);
        // Put next tab after current one.
        elementIndex = undefined;
        tabIndex = adoptedTab._tPos + 1;
      }
    }

    return this.addTabGroup(newTabs, {
      id: group.id,
      label: group.label,
      color: group.color,
      insertBefore: newTabs[0],
      isAdoptingGroup: true,
    });
  }

  /**
   * @param {MozSplitViewWrapper} container
   * @param {object} [options]
   * @param {number} [options.elementIndex]
   * @param {number} [options.tabIndex]
   * @param {boolean} [options.selectTab]
   * @returns {MozSplitViewWrapper}
   */
  adoptSplitView(container, { elementIndex, tabIndex, selectTab } = {}) {
    if (container.ownerDocument == this.document) {
      return container;
    }

    let oldSelectedTab =
      selectTab && container.documentGlobal.gBrowser.selectedTab;
    let newTabs = [];

    // When tabs are adopted across windows, they exit the tab split of the
    // source window, moved to the new window, and finally moved into a split
    // view in the new window. Although the splitViewId stays effectively the
    // same, the TabMove event fire a few times for these transitions.
    // To reduce noise in extension API events, we temporarily flag these
    // tabs to allow ext-tabs.js to filter out such TabMove events.
    for (let tab of container.tabs) {
      tab.removedByAdoption = true;
      let adoptedTab = this.adoptTab(tab, {
        selectTab: tab === oldSelectedTab,
        tabIndex,
        elementIndex,
      });
      adoptedTab.addedByAdoption = true;
      newTabs.push(adoptedTab);
      // Put next tab after current one.
      elementIndex = undefined;
      tabIndex = adoptedTab._tPos + 1;
    }

    try {
      return this.addTabSplitView(newTabs, {
        id: container.splitViewId,
        insertBefore: newTabs[0],
      });
    } finally {
      for (let tab of newTabs) {
        delete tab.addedByAdoption;
      }
    }
  }

  /**
   * Get all open tab groups from all windows. Does not include saved groups.
   *
   * @param {object} [options]
   * @param {boolean} [options.sortByLastSeenActive]
   *   Sort groups so that groups that have more recently seen and active
   *   tabs appear first. Defaults to false.
   */
  getAllTabGroups({ sortByLastSeenActive = false } = {}) {
    let groups = lazy.BrowserWindowTracker.getOrderedWindows({
      private: lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal),
    }).reduce(
      (acc, thisWindow) => acc.concat(thisWindow.gBrowser.tabGroups),
      []
    );
    if (sortByLastSeenActive) {
      groups.sort(
        (group1, group2) => group2.lastSeenActive - group1.lastSeenActive
      );
    }
    return groups;
  }

  getTabGroupById(id) {
    for (const win of lazy.BrowserWindowTracker.getOrderedWindows({
      private: lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal),
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
    if (
      createLazyBrowser &&
      uriString != "about:blank" &&
      uriString != "about:opentabs"
    ) {
      lazyBrowserURI = aURIObject;
      uriString = "about:blank";
    }

    let uriIsAboutBlank = uriString == "about:blank";
    return { uri: aURIObject, uriIsAboutBlank, lazyBrowserURI, uriString };
  }

  /**
   * @param {object} options
   * @param {string} [options.uriString]
   *   URI the tab is going to load, used as its initial label.
   * @param {number} [options.userContextId]
   *   The userContextId (container identifier) to open the tab in. Inherited
   *   from the opener tab when absent.
   * @param {MozTabbrowserTab} [options.openerTab]
   *   The tab that opened this one.
   * @param {boolean} [options.pinned]
   *   Create the tab pinned.
   * @param {boolean} [options.noInitialLabel]
   *   Leave the tab's label empty until the load provides a title.
   * @param {boolean} [options.skipBackgroundNotify]
   *   Don't draw attention to the tab when it wants to be selected while
   *   loading in the background.
   * @param {boolean} [options.animate]
   *   Whether the tab is going to be animated into place.
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
    var t = this.document.createXULElement("tab", {
      is: "tabbrowser-tab",
    });
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
      if (this.documentGlobal.isBlankPageURL(uriString)) {
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
      lazy.ContextualIdentityService.setTabStyle(t);
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
      UserInteraction.update(
        "browser.tabs.opening",
        "not-animated",
        this.documentGlobal
      );
      t.setAttribute("fadein", "true");

      // Call _handleNewTab asynchronously as it needs to know if the
      // new tab is selected.
      this.documentGlobal.setTimeout(
        function (tabContainer) {
          tabContainer._handleNewTab(t);
        },
        0,
        this.tabContainer
      );
    } else {
      UserInteraction.update(
        "browser.tabs.opening",
        "animated",
        this.documentGlobal
      );
    }

    return t;
  }

  /**
   * @param {MozTabbrowserTab} tab
   *   The tab to create the browser for.
   * @param {object} options
   * @param {string} [options.uriString]
   *   URI the browser is going to load, which decides its remote type.
   * @param {nsIURI} [options.uri]
   *   The same URI, used to pick a default icon for the tab.
   * @param {string} [options.name]
   *   Name of the window the browser represents, as passed to `window.open()`.
   * @param {string} [options.preferredRemoteType]
   *   Remote type to prefer, when the URI can load in more than one.
   * @param {MozBrowser} [options.openerBrowser]
   *   The browser that started the load, whose remote type is preferred when
   *   no other preference applies.
   * @param {boolean} [options.uriIsAboutBlank]
   *   Whether the browser starts out on about:blank.
   * @param {nsIReferrerInfo} [options.referrerInfo]
   *   Referrer info for the load, which decides the remote type of an
   *   about:blank load.
   * @param {boolean} [options.forceNotRemote]
   *   Create the browser in the parent process.
   * @param {number} [options.initialBrowsingContextGroupId]
   *   Browsing context group to create the browser in, which can place it in
   *   the same process as another window.
   * @param {nsIOpenWindowInfo} [options.openWindowInfo]
   *   Information about the content window that asked for this browser, when
   *   it comes from a `window.open()` call.
   * @param {boolean} [options.skipLoad]
   *   Create the browser without starting a load in it.
   * @param {string} [options.triggeringRemoteType]
   *   The remoteType triggering this load, preferred over predicting one.
   * @returns {{browser: MozBrowser, usingPreloadedContent: boolean}}
   */
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

    // If URI is about:blank and we don't have a preferred remote type,
    // then we need to use the referrer, if we have one, to get the
    // correct remote type for the new tab.
    if (
      uriIsAboutBlank &&
      !preferredRemoteType &&
      referrerInfo &&
      referrerInfo.originalReferrer
    ) {
      preferredRemoteType = ChromeUtils.predictRemoteTypeForURI(
        referrerInfo.originalReferrer,
        { window: this.documentGlobal, userContextId }
      );
    }

    let remoteType = forceNotRemote
      ? lazy.E10SUtils.NOT_REMOTE
      : ChromeUtils.predictRemoteTypeForURI(uriString, {
          window: this.documentGlobal,
          userContextId,
          preferredRemoteType,
        });

    let b,
      usingPreloadedContent = false;
    // If we open a new tab with the newtab URL in the default
    // userContext, check if there is a preloaded browser ready.
    if (
      uriString == this.documentGlobal.BROWSER_NEW_TAB_URL &&
      !userContextId
    ) {
      b = lazy.NewTabPagePreloading.getPreloadedBrowser(this.documentGlobal);
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

    this.#tabForBrowser.set(b, tab);
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

  /**
   * @param {MozBrowser} browser
   *   The browser to start the load in.
   * @param {object} options
   * @param {nsIURI} [options.uri]
   *   The URI to load, when it could be parsed.
   * @param {string} [options.uriString]
   *   The URI to load, as given by the caller.
   * @param {boolean} [options.usingPreloadedContent]
   *   Whether the browser already holds preloaded content for this URI, in
   *   which case there is nothing left to load.
   * @param {nsIPrincipal} options.triggeringPrincipal
   *   Triggering principal to pass to docshell for the load.
   * @param {nsIPrincipal} [options.originPrincipal]
   *   Origin principal to inherit when `uriString` is given and no preloaded
   *   browser is used.
   * @param {nsIPrincipal} [options.originStoragePrincipal]
   *   Origin storage principal to inherit when `uriString` is given and no
   *   preloaded browser is used.
   * @param {boolean} [options.uriIsAboutBlank]
   *   Whether the URI to load is about:blank, which the browser already shows.
   * @param {boolean} [options.allowInheritPrincipal]
   *   Allow the load to inherit the triggering principal.
   * @param {boolean} [options.allowThirdPartyFixup]
   *   Allow transforming the URI into a search query.
   * @param {boolean} [options.fromExternal]
   *   Whether the URI was supplied to Firefox from an external application.
   * @param {boolean} [options.forceAllowDataURI]
   *   Force allow a data URI to load as a toplevel load.
   * @param {boolean} [options.isCaptivePortalTab]
   *   Whether the tab loads a captive portal login page, whose load must not
   *   use DNS over HTTPS.
   * @param {boolean} [options.skipLoad]
   *   Leave the browser on its initial about:blank.
   * @param {nsIReferrerInfo} [options.referrerInfo]
   *   Referrer info for the request.
   * @param {string} [options.charset]
   *   Character set to use for the load. Legacy argument - do not use.
   * @param {nsIInputStream} [options.postData]
   *   Data to post as part of the request.
   * @param {nsIPolicyContainer} [options.policyContainer]
   *   The policy container that should apply to the load.
   * @param {object} [options.globalHistoryOptions]
   *   Used by :doc:`Places </browser/places/index>` to keep track of search
   *   related metadata for the load.
   * @param {string} [options.triggeringRemoteType]
   *   The remoteType triggering this load.
   * @param {nsILoadInfo_SchemelessInputType} [options.schemelessInput]
   *   Whether the search/URL term was without an explicit scheme.
   * @param {boolean} [options.hasValidUserGestureActivation]
   *   Indicates if a valid user gesture caused this load. This informs
   *   e.g. popup blocker decisions.
   * @param {boolean} [options.textDirectiveUserActivation]
   *   Whether a user gesture allows the load to scroll to a text fragment.
   */
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
      forceAllowDataURI,
      isCaptivePortalTab,
      skipLoad,
      referrerInfo,
      charset,
      postData,
      policyContainer,
      globalHistoryOptions,
      triggeringRemoteType,
      schemelessInput,
      hasValidUserGestureActivation,
      textDirectiveUserActivation,
    }
  ) {
    const shouldInheritSecurityContext = (() => {
      if (
        !usingPreloadedContent &&
        originPrincipal &&
        originStoragePrincipal &&
        uriString
      ) {
        let { URI_INHERITS_SECURITY_CONTEXT } = Ci.nsIProtocolHandler;
        // Unless we know for sure we're not inheriting principals,
        // force the about:blank viewer to have the right principal:
        if (
          !uri ||
          this.documentGlobal.doGetProtocolFlags(uri) &
            URI_INHERITS_SECURITY_CONTEXT
        ) {
          return true;
        }
      }
      return false;
    })();

    if (shouldInheritSecurityContext) {
      browser.createAboutBlankDocumentViewer(
        originPrincipal,
        originStoragePrincipal
      );
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
      if (uriString && !this.documentGlobal.gInitialPages.includes(uriString)) {
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
        // The purpose of LOAD_FLAGS_FIRST_LOAD is to close a new
        // tab if it turns out to be a download.
        loadFlags |= LOAD_FLAGS_FIRST_LOAD;
      }
      if (!allowInheritPrincipal) {
        loadFlags |= LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL;
      }
      if (isCaptivePortalTab) {
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
          policyContainer,
          globalHistoryOptions,
          triggeringRemoteType,
          schemelessInput,
          hasValidUserGestureActivation,
          textDirectiveUserActivation,
          isCaptivePortalTab,
        });
      } catch (ex) {
        console.error(ex);
      }
    }
  }

  /**
   * @typedef {object} TabGroupWorkingData
   * @property {TabGroupStateData} stateData
   *   Session state of the group being restored.
   * @property {MozTabbrowserTabGroup|undefined} node
   *   The group's element, once the first of its tabs has been created.
   * @property {DocumentFragment} containingTabsFragment
   *   Fragment collecting the group's tabs until the group is inserted.
   */

  /**
   * @typedef {number} TabSplitViewId
   *   Unique ID of a tab split view.
   */

  /**
   * @typedef {object} SplitViewWorkingData
   * @property {MozTabbrowserTab[]} tabs
   *   The split view's tabs, collected as they are created.
   * @property {MozTabSplitViewWrapper|undefined} node
   *   The split view's element, created once all of its tabs exist.
   * @property {number} numberOfTabs
   *   How many tabs the split view is expected to contain.
   */

  /**
   * @param {boolean} restoreTabsLazily
   * @param {number} selectTab see SessionStore.restoreTabs { aSelectTab }
   * @param {TabStateData[]} tabDataList
   * @param {TabGroupStateData[]} tabGroupDataList
   * @param {TabSplitViewStateData[]} splitViewDataList
   * @returns {MozTabbrowserTab[]}
   */
  createTabsForSessionRestore(
    restoreTabsLazily,
    selectTab,
    tabDataList,
    tabGroupDataList,
    splitViewDataList = []
  ) {
    let tabs = [];
    let tabsFragment = this.document.createDocumentFragment();
    let tabToSelect = null;
    let hiddenTabs = new Map();
    /** @type {Map<TabGroupId, TabGroupWorkingData>} */
    let tabGroupWorkingData = new Map();
    /** @type {Map<TabSplitViewId, SplitViewWorkingData>} */
    let splitViewWorkingData = new Map();

    for (const tabGroupData of tabGroupDataList) {
      tabGroupWorkingData.set(tabGroupData.id, {
        stateData: tabGroupData,
        node: undefined,
        containingTabsFragment: this.document.createDocumentFragment(),
      });
    }
    for (const splitViewData of splitViewDataList) {
      splitViewWorkingData.set(splitViewData.id, {
        numberOfTabs: splitViewData.numberOfTabs,
        node: undefined,
        tabs: [],
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
        !lazy.SessionStore.isTabRestoring(this.selectedTab) &&
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
        let createLazyBrowser = restoreTabsLazily && !select && !tabData.pinned;

        let url = "about:blank";
        if (tabData.entries?.length) {
          let activeIndex = (tabData.index || tabData.entries.length) - 1;
          // Ensure the index is in bounds.
          activeIndex = Math.min(activeIndex, tabData.entries.length - 1);
          activeIndex = Math.max(activeIndex, 0);
          url = tabData.entries[activeIndex].url;
        }

        let preferredRemoteType = ChromeUtils.predictRemoteTypeForURI(url, {
          window: this.documentGlobal,
          userContextId,
        });

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

      let splitView =
        tabData.splitViewId && splitViewWorkingData.get(tabData.splitViewId);
      if (splitView) {
        splitView.tabs.push(tab);
        // Only create the split view when we've got the last tab it would contain
        if (splitView.tabs.length == splitView.numberOfTabs) {
          splitView.node = this._createTabSplitView(tabData.splitViewId);
        }
      }

      tabs.push(tab);

      if (tabData.pinned) {
        this.pinTab(tab);
        // Then ensure all the tab open/pinning information is sent.
        this._fireTabOpen(tab, {});
      } else if (tabData.groupId) {
        let { groupId } = tabData;
        const tabGroup = tabGroupWorkingData.get(groupId);
        // if a tab refers to a tab group we don't know, skip any group
        // processing

        if (tabGroup) {
          if (!splitView) {
            tabGroup.containingTabsFragment.appendChild(tab);
          } else if (splitView?.node) {
            tabGroup.containingTabsFragment.appendChild(splitView.node);
          }

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

        if (!splitView) {
          tabsFragment.appendChild(tab);
        } else if (splitView?.node) {
          tabsFragment.appendChild(splitView.node);
        }

        if (tabWasReused) {
          this.tabContainer._invalidateCachedTabs();
        }
      }

      tab.initialize();
    }

    // inject the top-level tab and tab group DOM nodes
    this.tabContainer.appendChild(tabsFragment);

    // inject tab DOM nodes into the now-connected tab group and split view DOM nodes
    for (const tabGroup of tabGroupWorkingData.values()) {
      if (tabGroup.node) {
        tabGroup.node.appendChild(tabGroup.containingTabsFragment);
      }
    }
    for (const splitView of splitViewWorkingData.values()) {
      if (splitView.node) {
        splitView.node.addTabs(splitView.tabs, { isSessionRestore: true });
      }
    }

    for (let [tab, hiddenBy] of hiddenTabs) {
      let event = this.document.createEvent("Events");
      event.initEvent("TabHide", true, false);
      tab.dispatchEvent(event);
      if (hiddenBy) {
        lazy.SessionStore.setCustomTabValue(tab, "hiddenBy", hiddenBy);
      }
    }

    this.tabContainer._invalidateCachedTabs();

    // We need to wait until after all tabs have been appended to the DOM
    // to remove the old selected tab.
    if (tabToSelect) {
      let leftoverTab = this.selectedTab;
      this.selectedTab = tabToSelect;
      this.removeTab(leftoverTab);
    }

    if (tabs.length > 1 || !tabs[0].selected) {
      this._updateTabsAfterInsert();
      this.documentGlobal.TabBarVisibility.update();

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
            var evt = new this.documentGlobal.CustomEvent(
              "TabBrowserInserted",
              {
                bubbles: true,
                detail: { insertedOnTabCreation: true },
              }
            );
            tab.dispatchEvent(evt);
          }
        }
      }
    }

    return tabs;
  }

  moveTabsToStart(contextTab, { metricsContext } = {}) {
    let tabs = contextTab.multiselected ? this.selectedTabs : [contextTab];
    this.recordTabMetrics(this.TabMetrics.METRIC_ACTION.MOVE, metricsContext, {
      tabCount: tabs.length,
    });
    // Walk the array in reverse order so the tabs are kept in order.
    for (let i = tabs.length - 1; i >= 0; i--) {
      this.moveTabToStart(tabs[i], {
        metricsContext: this.TabMetrics.decomposedContext(metricsContext),
      });
    }
  }

  moveTabsToEnd(contextTab, { metricsContext } = {}) {
    let tabs = contextTab.multiselected ? this.selectedTabs : [contextTab];
    this.recordTabMetrics(this.TabMetrics.METRIC_ACTION.MOVE, metricsContext, {
      tabCount: tabs.length,
    });
    for (let tab of tabs) {
      this.moveTabToEnd(tab, {
        metricsContext: this.TabMetrics.decomposedContext(metricsContext),
      });
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

      this.documentGlobal.focus();
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
        this.documentGlobal,
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
    if (aCloseTabs != this.closingTabsEnum.ALL && tabsToClose <= maxTabsUndo) {
      return true;
    }

    // Our prompt to close this window is most important, so replace others.
    this.documentGlobal.gDialogBox.replaceDialogIfOpen();

    // default to true: if it were false, we wouldn't get this far
    var warnOnClose = { value: true };

    // focus the window before prompting.
    // this will raise any minimized window, which will
    // make it obvious which window the prompt is for and will
    // solve the problem of windows "obscuring" the prompt.
    // see bug #350299 for more details
    this.documentGlobal.focus();
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
      this.documentGlobal,
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
   * @param {object} [options]
   * @param {number} [options.elementIndex]
   * @param {number} [options.tabIndex]
   * @param {MozTabbrowserTab} [options.ownerTab]
   *   The tab to select when this tab is closed.
   * @param {MozTabbrowserTab} [options.openerTab]
   *   The tab that opened this one, next to which it goes when the
   *   `browser.tabs.insertRelatedAfterCurrent` pref is set.
   * @param {boolean} [options.pinned]
   *   Whether the tab is pinned.
   * @param {boolean} [options.bulkOrderedOpen]
   *   Keep the tab in the order it was opened in rather than inserting it
   *   next to the current tab, as wanted when opening a set of tabs at once.
   * @param {MozTabbrowserTabGroup} [options.tabGroup]
   *   A related tab group where this tab should be added, when applicable.
   */
  #insertTabAtIndex(
    tab,
    {
      tabIndex,
      elementIndex,
      ownerTab,
      openerTab,
      pinned,
      bulkOrderedOpen,
      tabGroup,
    } = {}
  ) {
    // If this new tab is owned by another, assert that relationship
    if (ownerTab) {
      tab.owner = ownerTab;
    }

    // Ensure we have an index if one was not provided.
    if (typeof elementIndex != "number" && typeof tabIndex != "number") {
      // Move the new tab after another tab if needed, to the end otherwise.
      elementIndex = Infinity;
      let insertRelatedAfterCurrent = Services.prefs.getBoolPref(
        "browser.tabs.insertRelatedAfterCurrent"
      );
      if (
        !bulkOrderedOpen &&
        ((openerTab && insertRelatedAfterCurrent) ||
          Services.prefs.getBoolPref("browser.tabs.insertAfterCurrent"))
      ) {
        let lastRelatedTab =
          openerTab && this.#lastRelatedTabMap.get(openerTab);
        let previousTab =
          (insertRelatedAfterCurrent && lastRelatedTab) ||
          openerTab ||
          this.selectedTab;
        if (!tabGroup) {
          tabGroup = previousTab.group;
        }
        if (
          Services.prefs.getBoolPref(
            "browser.tabs.insertAfterCurrentExceptPinned"
          ) &&
          previousTab.pinned
        ) {
          elementIndex = Infinity;
        } else if (previousTab.visible && previousTab.splitview) {
          elementIndex =
            this.tabContainer.dragAndDropElements.indexOf(
              previousTab.splitview
            ) + 1;
        } else if (previousTab.visible) {
          elementIndex = previousTab.elementIndex + 1;
        } else if (previousTab == this.documentGlobal.FirefoxViewHandler.tab) {
          elementIndex = 0;
        }

        if (lastRelatedTab) {
          lastRelatedTab.owner = null;
        } else if (openerTab) {
          tab.owner = openerTab;
        }
        // Always set related map if opener exists.
        if (openerTab) {
          this.#lastRelatedTabMap.set(openerTab, tab);
        }
      }
    }

    let allItems;
    let index;
    if (typeof elementIndex == "number") {
      allItems = this.tabContainer.dragAndDropElements;
      index = elementIndex;
    } else {
      allItems = this.tabs;
      index = tabIndex;
    }
    // Ensure index is within bounds.
    if (tab.pinned) {
      index = Math.max(index, 0);
      index = Math.min(index, this.pinnedTabCount);
    } else {
      index = Math.max(index, this.pinnedTabCount);
      index = Math.min(index, allItems.length);
    }
    /** @type {MozTabbrowserTab|undefined} */
    let itemAfter = allItems.at(index);

    if (pinned && !itemAfter?.pinned) {
      itemAfter = null;
    } else if (itemAfter?.splitview) {
      let splitview = itemAfter.splitview;
      itemAfter =
        itemAfter === splitview.tabs[0]
          ? splitview
          : splitview.nextElementSibling || null;
    }
    // Prevent a flash of unstyled content by setting up the tab content
    // and inherited attributes before appending it (see Bug 1592054):
    tab.initialize();

    this.tabContainer._invalidateCachedTabs();

    if (tabGroup) {
      if (
        (this.isTab(itemAfter) && itemAfter.group == tabGroup) ||
        this.isSplitViewWrapper(itemAfter)
      ) {
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
      const tabContainer = pinned
        ? this.tabContainer.pinnedTabsContainer
        : this.tabContainer;
      tabContainer.insertBefore(tab, itemAfter);
    }

    if (tab.group?.collapsed) {
      // Bug 1997096: automatically expand the group if we are adding a new
      // tab to a collapsed group, and that tab does not have automatic focus
      // (i.e. if the user right clicks and clicks "Open in New Tab")
      tab.group.collapsed = false;
    }

    this._updateTabsAfterInsert();

    if (pinned) {
      this._updateTabBarForPinnedTabs();
    }

    this.documentGlobal.TabBarVisibility.update();
  }

  /**
   * Dispatch a new tab event. This should be called when things are in a
   * consistent state, such that listeners of this event can again open
   * or close tabs.
   *
   * @param {MozTabbrowserTab} tab
   * @param {object} [eventDetail]
   *   Additional information to include in the event's `detail`.
   */
  _fireTabOpen(tab, eventDetail) {
    delete tab.initializingTab;
    let evt = new this.documentGlobal.CustomEvent("TabOpen", {
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
    /** @type {Map<string, Set<number>>} */
    let userContextIdsPerUri = new Map();
    for (let tab of lastSeenTabs) {
      const uri = tab.linkedBrowser?.currentURI;
      if (!uri) {
        // Can't tell if it's a duplicate without a URI.
        // Safest to leave it be.
        continue;
      }
      let userContextIds = userContextIdsPerUri.getOrInsertComputed(
        uri.spec,
        () => new Set()
      );
      let userContextId = tab.userContextId;
      if (!tab.pinned && userContextIds.has(userContextId)) {
        duplicateTabs.push(tab);
      }
      userContextIds.add(userContextId);
    }
    return duplicateTabs;
  }

  removeDuplicateTabs(aTab, options) {
    this._removeDuplicateTabs(
      aTab,
      this.getDuplicateTabsToClose(aTab),
      this.closingTabsEnum.DUPLICATES,
      options
    );
  }

  _removeDuplicateTabs(aConfirmationAnchor, tabs, aCloseTabs, options) {
    if (!tabs.length) {
      return;
    }

    if (!this.warnAboutClosingTabs(tabs.length, aCloseTabs)) {
      return;
    }

    this.removeTabs(tabs, options);
    this.documentGlobal.ConfirmationHint.show(
      aConfirmationAnchor,
      "confirmation-hint-duplicate-tabs-closed",
      { l10nArgs: { tabCount: tabs.length } }
    );
  }

  /**
   * @param {object} [options]
   * @param {Element} [options.confirmationAnchor]
   *   What the "closed N tabs" hint should point at. Callers outside the All
   *   Tabs menu need to pass their own button, since the All Tabs button is
   *   not necessarily on the toolbar and the hint lands in the window's
   *   corner without an anchor.
   */
  removeAllDuplicateTabs({
    confirmationAnchor = this.document.getElementById("alltabs-button"),
  } = {}) {
    this._removeDuplicateTabs(
      confirmationAnchor,
      this.getAllDuplicateTabsToClose(),
      this.closingTabsEnum.ALL_DUPLICATES
    );
  }

  /**
   * In a multi-select context, the tabs (except pinned tabs) that are located to the
   * left of the leftmost selected tab will be removed.
   *
   * @param {MozTabbrowserTab} aTab
   * @param {object} [options]
   *   Options to use when removing tabs. @see removeTabs for more info.
   */
  removeTabsToTheStartFrom(aTab, options) {
    let tabs = this._getTabsToTheStartFrom(aTab);
    if (
      !this.warnAboutClosingTabs(tabs.length, this.closingTabsEnum.TO_START)
    ) {
      return;
    }

    this.removeTabs(tabs, options);
  }

  /**
   * In a multi-select context, the tabs (except pinned tabs) that are located to the
   * right of the rightmost selected tab will be removed.
   *
   * @param {MozTabbrowserTab} aTab
   * @param {object} [options]
   *   Options to use when removing tabs. @see removeTabs for more info.
   */
  removeTabsToTheEndFrom(aTab, options) {
    let tabs = this._getTabsToTheEndFrom(aTab);
    if (!this.warnAboutClosingTabs(tabs.length, this.closingTabsEnum.TO_END)) {
      return;
    }

    this.removeTabs(tabs, options);
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
    let { skipWarnAboutClosingTabs = false, skipPinnedOrSelectedTabs = true } =
      aParams;

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

  removeMultiSelectedTabs({ metricsContext, excludePinnedTabs } = {}) {
    let selectedTabs = this.selectedTabs;
    if (
      !this.warnAboutClosingTabs(
        selectedTabs.length,
        this.closingTabsEnum.MULTI_SELECTED
      )
    ) {
      return;
    }

    if (excludePinnedTabs) {
      selectedTabs = this.selectedTabs.filter(tab => !tab.pinned);
      this.removeTabs(selectedTabs, { metricsContext });
      this.clearMultiSelectedTabs();
      if (
        this.selectedTab.pinned &&
        this.visibleTabs.length > this.pinnedTabCount
      ) {
        // Select first unpinned tab if the selected tab is pinned.
        this.tabContainer.selectedIndex = this.pinnedTabCount;
      }
    } else {
      this.removeTabs(selectedTabs, { metricsContext });
    }
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
   *   This will suppress the warning about closing a window with the last tab.
   * @param {boolean} options.skipPermitUnload
   *   Skips the before unload checks for the tabs. Only set this to true when
   *   using it in tandem with `runBeforeUnloadForTabs`.
   * @param {boolean} options.skipRemoves
   *   Skips actually removing the tabs. The beforeunload handlers still run.
   * @param {boolean} options.skipSessionStore
   *   If true, don't record the closed tabs in SessionStore.
   * @param {TabMetricsContext} options.metricsContext
   *   The context for the operation for telemetry purposes.
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
      metricsContext,
    }
  ) {
    // Note: if you change any of the unload algorithm, consider also
    // changing `runBeforeUnloadForTabs` above.
    /** @type {MozTabbrowserTab[]} */
    let tabsWithBeforeUnloadPrompt = [];
    /** @type {MozTabbrowserTab[]} */
    let tabsWithoutBeforeUnload = [];
    /** @type {Array<Promise<void>>} */
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
              Glean.browserTabclose.permitUnloadTime.stopAndAccumulate(timerId);
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
              Glean.browserTabclose.permitUnloadTime.stopAndAccumulate(timerId);
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
          metricsContext,
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
   *
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
     *
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
   *   This will suppress the warning about closing a window with the last tab.
   * @param {boolean} [options.skipPermitUnload]
   *   Skips the before unload checks for the tabs. Only set this to true when
   *   using it in tandem with `runBeforeUnloadForTabs`.
   * @param {boolean}  [options.skipSessionStore]
   *   If true, don't record the closed tabs in SessionStore.
   * @param {boolean} [options.skipGroupCheck]
   *   Skip separate processing of whole tab groups from the set of tabs.
   *   Used by removeTabGroup.
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes
   */
  removeTabs(
    tabs,
    {
      animate = true,
      suppressWarnAboutClosingWindow = false,
      skipPermitUnload = false,
      skipSessionStore = false,
      skipGroupCheck = false,
      metricsContext,
    } = {}
  ) {
    // When 'closeWindowWithLastTab' pref is enabled, closing all tabs
    // can be considered equivalent to closing the window.
    if (
      this.tabs.length == tabs.length &&
      Services.prefs.getBoolPref("browser.tabs.closeWindowWithLastTab")
    ) {
      this.documentGlobal.closeWindow(
        true,
        suppressWarnAboutClosingWindow
          ? null
          : this.documentGlobal.warnAboutClosingWindow,
        "close-last-tab"
      );
      return;
    }

    if (!skipSessionStore) {
      lazy.SessionStore.resetLastClosedTabCount(this.documentGlobal);
    }
    this.#clearMultiSelectionLocked = true;

    let closedTabCount = tabs.length;

    // Tracks a snapshot of each whole-group's tabs so we can decrement
    // closedTabCount for any whose beforeunload handler cancels the close.
    let groupTabsToClose = [];
    let groupRemovalPromises = [];

    // Guarantee that #clearMultiSelectionLocked lock gets released.
    try {
      // If selection includes entire groups, we might want to save them
      if (!skipGroupCheck) {
        let [groups, leftoverTabs] = this.#separateWholeGroups(tabs);
        groupRemovalPromises = groups.map(group => {
          groupTabsToClose.push(...group.tabs);
          if (!skipSessionStore) {
            group.save();
          }

          return this.removeTabGroup(group, {
            animate,
            skipSessionStore,
            skipPermitUnload,
            metricsContext: this.TabMetrics.decomposedContext(metricsContext),
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
          metricsContext: this.TabMetrics.decomposedContext(metricsContext),
        });

      // Wait for all the beforeunload events to have been processed by content
      // processes, including those run inside group removals. The permitUnload()
      // promise will, alas, not call its resolution callbacks after the browser
      // window the promise lives in has closed, so we have to check for that
      // case explicitly.
      let done = false;
      Promise.all([...groupRemovalPromises, beforeUnloadComplete]).then(() => {
        done = true;
      });
      Services.tm.spinEventLoopUntilOrQuit(
        "tabbrowser.js:removeTabs",
        () => done || this.documentGlobal.closed
      );
      if (!done) {
        return;
      }

      // Decrement for any group tabs whose beforeunload handler cancelled the
      // close — those will still have tab.closing == false.
      for (let tab of groupTabsToClose) {
        if (!tab.closing) {
          closedTabCount--;
        }
      }

      let aParams = {
        animate,
        prewarmed: true,
        skipPermitUnload,
        skipSessionStore,
        metricsContext: this.TabMetrics.decomposedContext(metricsContext),
      };

      // Now run again sequentially the beforeunload listeners that will result in a prompt.
      for (let tab of tabsWithBeforeUnloadPrompt) {
        this.removeTab(tab, aParams);
        if (!tab.closing) {
          // If we abort the closing of the tab.
          tab._closedInMultiselection = false;
          closedTabCount -= 1;
        }
      }

      // Avoid changing the selected browser several times by removing it,
      // if appropriate, lastly.
      if (lastToClose) {
        this.removeTab(lastToClose, aParams);
        if (!lastToClose.closing) {
          closedTabCount -= 1;
        }
      }

      if (closedTabCount > 0) {
        this.recordTabMetrics(
          this.TabMetrics.METRIC_ACTION.CLOSE,
          metricsContext,
          { tabCount: closedTabCount }
        );
      }
    } catch (e) {
      console.error(e);
    }

    this.#clearMultiSelectionLocked = false;
    this._avoidSingleSelectedTab();
  }

  removeCurrentTab(aParams) {
    this.removeTab(this.selectedTab, aParams);
  }

  /**
   * A tab which never showed anything but a blank page and has no history
   * only existed for the load which has been moved away, so close it rather
   * than leave it behind empty. Closing the last tab of a window is left out:
   * that would close the window.
   *
   * @param {MozBrowser} aBrowser The browser the load started in.
   */
  maybeCloseTabForRetargetedLoad(aBrowser) {
    let tab = this.getTabForBrowser(aBrowser);
    if (
      !tab ||
      tab.pinned ||
      this.tabs.length === 1 ||
      !tab.isEmptyIgnoringLoad
    ) {
      return;
    }

    this.removeTab(tab, { skipPermitUnload: true });
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
      metricsContext,
    } = {}
  ) {
    if (UserInteraction.running("browser.tabs.opening", this.documentGlobal)) {
      UserInteraction.finish("browser.tabs.opening", this.documentGlobal);
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
    // We have to sample the tab width now, since _beginRemoveTab might
    // end up modifying the DOM in such a way that aTab gets a new
    // frame created for it (for example, by updating the visually selected
    // state).
    let tabWidth =
      this.documentGlobal.windowUtils.getBoundsWithoutFlushing(aTab).width;
    let isLastTab = this.#isLastTabInWindow(aTab);
    if (
      !this._beginRemoveTab(aTab, {
        closeWindowFastpath: true,
        skipPermitUnload,
        closeWindowWithLastTab,
        prewarmed,
        skipSessionStore,
        metricsContext,
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
      this.documentGlobal.gReduceMotion ||
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

    this.documentGlobal.setTimeout(
      function (tab, tabbrowser) {
        if (
          tab.container &&
          tabbrowser.documentGlobal.getComputedStyle(tab).maxWidth == "0.1px"
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

  /**
   * Whether closing the window's last tab should close the window rather
   * than leave a new empty tab behind.
   *
   * @returns {boolean}
   */
  get #shouldCloseWindowWithLastTab() {
    return (
      !this.documentGlobal.toolbar.visible ||
      Services.prefs.getBoolPref("browser.tabs.closeWindowWithLastTab")
    );
  }

  /**
   * Returns `true` if `tab` is the last tab in this window. This logic is
   * intended for cases like determining if a window should close due to `tab`
   * being closed, therefore hidden tabs are not considered in this function.
   *
   * Note: must be called before `tab` is closed/closing.
   *
   * @param {MozTabbrowserTab} tab
   * @returns {boolean}
   */
  #isLastTabInWindow(tab) {
    for (const otherTab of this.tabs) {
      if (otherTab != tab && otherTab.isOpen && !otherTab.hidden) {
        return false;
      }
    }
    return true;
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
      metricsContext = this.TabMetrics.UNKNOWN_CONTEXT,
    } = {}
  ) {
    if (aTab.closing || this.#windowIsClosing) {
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
      aTab == this.selectedTab && browser._sharingState?.webRTC?.screen;

    if (!screenShareInActiveTab) {
      this._blurTab(aTab);
    }

    var closeWindow = false;
    var newTab = false;
    if (this.#isLastTabInWindow(aTab)) {
      closeWindow =
        closeWindowWithLastTab ?? this.#shouldCloseWindowWithLastTab;

      if (closeWindow) {
        // We've already called beforeunload on all the relevant tabs if we get here,
        // so avoid calling it again:
        this.documentGlobal.skipNextCanClose = true;
      }

      // Closing the tab and replacing it with a blank one is notably slower
      // than closing the window right away. If the caller opts in, take
      // the fast path.
      if (closeWindow && closeWindowFastpath && !this._removingTabs.size) {
        // This call actually closes the window, unless the user
        // cancels the operation.  We are finished here in both cases.
        this.#windowIsClosing = this.documentGlobal.closeWindow(
          true,
          this.documentGlobal.warnAboutClosingWindow,
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
        const filter = this.#tabFilters.get(aTab);
        browser.webProgress.removeProgressListener(filter);
        const listener = this.#tabListeners.get(aTab);
        filter.removeProgressListener(listener);
        listener.destroy();
        this.#tabListeners.delete(aTab);
        this.#tabFilters.delete(aTab);
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
      // Deliberately leave tab.muted alone: that attribute is what records a
      // real mute and what session restore persists, so undo-close-tab
      // returns the tab to unmuted.
      aTab.linkedBrowser.browsingContext?.mediaController?.mute();
    }

    aTab.closing = true;
    this._removingTabs.add(aTab);
    this.tabContainer._invalidateCachedTabs();

    // Invalidate hovered tab state tracking for this closing tab. The pointer
    // stays put while the tab goes away, so hand the hover over to the tab
    // moving into its place: no mouseover event is guaranteed to report it.
    // `_hover` alone isn't evidence that the pointer is still there, since no
    // mouseout is delivered while a drag session is in progress or when the
    // hovered tab's frame goes away, so check with the event state manager
    // before handing a hover over: a hover the event state manager doesn't
    // know about is one it will never send a mouseout to end.
    let handOverHover = aTab._hover && this.tabContainer.matches(":hover");
    aTab._mouseleave();
    if (handOverHover) {
      this.#tabTakingPlaceOf(aTab)?._mouseenter({
        withoutPointerEvent: true,
      });
    }

    if (newTab) {
      this.addTrustedTab(this.documentGlobal.BROWSER_NEW_TAB_URL, {
        skipAnimation: true,
        // In the event that insertAfterCurrent is set and the current tab is
        // inside a group that is being closed we want to avoid creating the
        // new tab inside that group.
        tabIndex: 0,
      });
    } else {
      this.documentGlobal.TabBarVisibility.update();
    }

    // Splice this tab out of any lines of succession before any events are
    // dispatched.
    this.replaceInSuccession(aTab, aTab.successor);
    this.setSuccessor(aTab, null);

    // We're committed to closing the tab now.
    // Dispatch a notification.
    // We dispatch it before any teardown so that event listeners can
    // inspect the tab that's about to close.
    let evt = new this.documentGlobal.CustomEvent("TabClose", {
      bubbles: true,
      detail: {
        adoptedBy: adoptedByTab,
        skipSessionStore,
        metricsContext,
      },
    });
    aTab.dispatchEvent(evt);

    this.recordTabMetrics(this.TabMetrics.METRIC_ACTION.CLOSE, metricsContext, {
      tabCount: 1,
    });

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
      if (!adoptedByTab && !this.documentGlobal.gMultiProcessBrowser) {
        // Prevent this tab from showing further dialogs, since we're closing it
        browser.contentWindow.windowUtils.disableDialogs();
      }

      // Remove the tab's filter and progress listener.
      const filter = this.#tabFilters.get(aTab);

      browser.webProgress.removeProgressListener(filter);

      const listener = this.#tabListeners.get(aTab);
      filter.removeProgressListener(listener);
      listener.destroy();
    }

    if (browser.registeredOpenURI && !adoptedByTab) {
      let userContextId = browser.getAttribute("usercontextid") || 0;
      this.UrlbarProviderOpenTabs.unregisterOpenTab(
        browser.registeredOpenURI.spec,
        userContextId,
        aTab.group?.id,
        lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
      );
      delete browser.registeredOpenURI;
    }

    // We are no longer the primary content area.
    browser.removeAttribute("primary");

    return true;
  }

  /**
   * @param {MozTabbrowserTab} closingTab
   * @returns {MozTabbrowserTab|null}
   *   The tab moving into `closingTab`'s spot in the tab strip, or null if a
   *   tab group label takes it or there's nothing after it.
   */
  #tabTakingPlaceOf(closingTab) {
    // Closing tabs are excluded from `ariaFocusableItems`, so the first item
    // following `closingTab` is the one moving up into its spot.
    let item = this.tabContainer.ariaFocusableItems.find(
      candidate =>
        closingTab.compareDocumentPosition(candidate) &
        Node.DOCUMENT_POSITION_FOLLOWING
    );
    return this.isTab(item) ? item : null;
  }

  _endRemoveTab(aTab) {
    if (!aTab || !aTab._endRemoveArgs) {
      return;
    }

    var [aCloseWindow, aNewTab] = aTab._endRemoveArgs;
    aTab._endRemoveArgs = null;

    if (this.#windowIsClosing) {
      aCloseWindow = false;
      aNewTab = false;
    }

    this.tabAnimationsInProgress--;

    this.#lastRelatedTabMap = new WeakMap();

    // update the UI early for responsiveness
    aTab.collapsed = true;
    this._blurTab(aTab);

    this._removingTabs.delete(aTab);

    if (aCloseWindow) {
      this.#windowIsClosing = true;
      for (let tab of this._removingTabs) {
        this._endRemoveTab(tab);
      }
    } else if (!this.#windowIsClosing) {
      if (aNewTab) {
        this.documentGlobal.gURLBar.select();
      }
    }

    // We're going to remove the tab and the browser now.
    this.#tabFilters.delete(aTab);
    this.#tabListeners.delete(aTab);

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

    // Remove the tab ...
    aTab.remove();
    this.tabContainer._invalidateCachedTabs();

    // ... and fix up the _tPos properties immediately.
    for (let i = aTab._tPos; i < this.tabs.length; i++) {
      this.tabs[i]._tPos = i;
    }

    if (!this.#windowIsClosing) {
      // update tab close buttons state
      this.tabContainer._updateCloseButtons();

      this.documentGlobal.setTimeout(
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
    this.#tabForBrowser.delete(aTab.linkedBrowser);
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
      this.#windowIsClosing = this.documentGlobal.closeWindow(
        true,
        this.documentGlobal.warnAboutClosingWindow,
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
    if (tabs.some(tab => tab.selected || tab.splitview?.hasActiveTab)) {
      // Unloading the currently selected tab.
      // Need to select a different one before unloading.
      // Avoid selecting any tab we're unloading now or
      // any tab that is already unloaded.
      unloadSelectedTab = true;
      let tabsToExclude = tabs.concat(
        this.tabContainer.allTabs.filter(tab => !tab.linkedPanel)
      );
      for (const tab of tabs) {
        if (tab.splitview) {
          tabsToExclude.push(
            ...tab.splitview.tabs.filter(splitViewTab => splitViewTab != tab)
          );
        }
      }
      let newTab = this._findTabToBlurTo(this.selectedTab, tabsToExclude);
      if (newTab) {
        this.selectedTab = newTab;
      } else {
        allTabsUnloaded = true;
        // all tabs are unloaded - show Firefox View if it's present, otherwise open a new tab
        // Firefox View counts as present if its tab is already open, or if the button
        // is visible, so as to not do this in private browsing mode or if the user
        // has removed the button from their toolbar (bug 1946432, bug 1989429)
        let firefoxViewAvailable =
          this.documentGlobal.FirefoxViewHandler.tab ||
          this.documentGlobal.FirefoxViewHandler.button?.checkVisibility({
            checkVisibilityCSS: true,
            visibilityProperty: true,
          });
        if (firefoxViewAvailable) {
          this.documentGlobal.FirefoxViewHandler.openTab("opentabs");
        } else {
          this.selectedTab = this.addTrustedTab(
            this.documentGlobal.BROWSER_NEW_TAB_URL,
            {
              skipAnimation: true,
            }
          );
        }
      }
    }
    let memoryUsageBeforeUnload = await getTotalMemoryUsage();
    let timeBeforeUnload = this.documentGlobal.performance.now();
    let numberOfTabsUnloaded = 0;
    await Promise.all(tabs.map(tab => this.prepareDiscardBrowser(tab)));

    for (let tab of tabs) {
      numberOfTabsUnloaded += this.discardBrowser(tab, true) ? 1 : 0;
    }
    let timeElapsed = Math.floor(
      this.documentGlobal.performance.now() - timeBeforeUnload
    );
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
   *
   * @param {Element} node
   *        The element that was clicked
   * @param {MouseEvent} event
   *        The click event
   */
  handleNewTabMiddleClick(node, event) {
    // We should be using the disabled property here instead of the attribute,
    // but some elements that this function is used with don't support it (e.g.
    // menuitem).
    if (node.hasAttribute("disabled")) {
      return;
    } // Do nothing

    if (event.button == 1) {
      this.documentGlobal.BrowserCommands.openTab({ event });
      // Stop the propagation of the click event, to prevent the event from being
      // handled more than once.
      // E.g. see https://bugzilla.mozilla.org/show_bug.cgi?id=1657992#c4
      event.stopPropagation();
      event.preventDefault();
    }
  }

  /**
   * Finds the tab that we will blur to if we blur aTab.
   *
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
    if (this.documentGlobal.FirefoxViewHandler.tab) {
      aExcludeTabs.push(this.documentGlobal.FirefoxViewHandler.tab);
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

    if (Services.prefs.getBoolPref("browser.tabs.selectMRUOnClose", false)) {
      let mruTab = remainingTabs
        .filter(t => t !== aTab)
        .reduce(
          (best, t) => (!best || t.lastAccessed > best.lastAccessed ? t : best),
          null
        );
      if (mruTab) {
        return mruTab;
      }
    }

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
   * Swaps the browsers of two tabs, then closes the other tab.
   *
   * @param {MozTabbrowserTab} aOurTab
   *   The tab in this window, which takes over the other tab's browser.
   * @param {MozTabbrowserTab} aOtherTab
   *   The tab to take the browser from, which may be in another window.
   * @returns {boolean}
   *   False if swapping isn't permitted, true otherwise.
   */
  swapBrowsersAndCloseOther(aOurTab, aOtherTab) {
    // Do not allow transfering a private tab to a non-private window
    // and vice versa.
    if (
      lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal) !=
      lazy.PrivateBrowsingUtils.isWindowPrivate(aOtherTab.documentGlobal)
    ) {
      return false;
    }

    // Do not allow transfering a useRemoteSubframes tab to a
    // non-useRemoteSubframes window and vice versa.
    if (
      this.documentGlobal.gFissionBrowser !=
      aOtherTab.documentGlobal.gFissionBrowser
    ) {
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
    var remoteBrowser = aOtherTab.documentGlobal.gBrowser;
    var isPending = aOtherTab.hasAttribute("pending");

    let otherTabListener = remoteBrowser.#tabListeners.get(aOtherTab);
    let stateFlags = 0;
    if (otherTabListener) {
      stateFlags = otherTabListener._stateFlags;
    }

    // Expedite the removal of the icon if it was already scheduled.
    if (aOtherTab._soundPlayingAttrRemovalTimer) {
      aOtherTab.documentGlobal.clearTimeout(
        aOtherTab._soundPlayingAttrRemovalTimer
      );
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
      let win = aOtherTab.documentGlobal;
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
        ourBrowser.browsingContext?.mediaController?.mute();
      }
      modifiedAttrs.push("muted");
    }
    if (aOtherTab.hasAttribute("discarded")) {
      aOurTab.toggleAttribute("discarded", true);
      modifiedAttrs.push("discarded");
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
      ourBrowser._sharingState = otherBrowser._sharingState;
      lazy.webrtcUI.swapBrowserForNotification(otherBrowser, ourBrowser);
    }
    if (aOtherTab.hasAttribute("pictureinpicture")) {
      aOurTab.toggleAttribute("pictureinpicture", true);
      modifiedAttrs.push("pictureinpicture");

      let event = new this.documentGlobal.CustomEvent(
        "TabSwapPictureInPicture",
        {
          detail: aOurTab,
        }
      );
      aOtherTab.dispatchEvent(event);
    }

    // Copy tab note-related properties of the tab.
    aOurTab.hasTabNote = aOtherTab.hasTabNote;
    aOurTab.canonicalUrl = aOtherTab.canonicalUrl;

    if (otherBrowser.isDistinctProductPageVisit) {
      ourBrowser.isDistinctProductPageVisit = true;
    }

    let srcBrowserId = otherBrowser.browserId;

    // Add a reference to the original registeredOpenURI to the closing
    // tab so that events operating on the tab before close can reference it.
    aOtherTab._originalRegisteredOpenURI = otherBrowser.registeredOpenURI;

    // If the other tab is pending (i.e. has not been restored, yet)
    // then do not switch docShells but retrieve the other tab's state
    // and apply it to our tab.
    if (isPending) {
      // Tag tab so that the extension framework can ignore tab events that
      // are triggered amidst the tab/browser restoration process
      // (TabHide, TabPinned, TabUnpinned, "muted" attribute changes, etc.).
      aOurTab.initializingTab = true;
      delete ourBrowser._cachedCurrentURI;
      lazy.SessionStore.setTabState(
        aOurTab,
        lazy.SessionStore.getTabState(aOtherTab)
      );
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

    lazy.SitePermissions.copyTemporaryPermissions(
      srcBrowserId,
      otherBrowser,
      ourBrowser
    );

    // Unregister the previously opened URI
    if (otherBrowser.registeredOpenURI) {
      let userContextId = otherBrowser.getAttribute("usercontextid") || 0;
      this.UrlbarProviderOpenTabs.unregisterOpenTab(
        otherBrowser.registeredOpenURI.spec,
        userContextId,
        aOtherTab.group?.id,
        lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
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
      aOtherTab.documentGlobal.close();
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
    let filter = otherTabBrowser.#tabFilters.get(aOtherTab);
    let tabListener = otherTabBrowser.#tabListeners.get(aOtherTab);
    otherBrowser.webProgress.removeProgressListener(filter);
    filter.removeProgressListener(tabListener);

    // Perform the docshell swap through the common mechanism.
    this._swapBrowserDocShells(aOurTab, otherBrowser);

    // Restore the listeners for the swapped in tab.
    tabListener = new TabProgressListener(
      aOtherTab,
      otherBrowser,
      false,
      false
    );
    otherTabBrowser.#tabListeners.set(aOtherTab, tabListener);

    const notifyAll = Ci.nsIWebProgress.NOTIFY_ALL;
    filter.addProgressListener(tabListener, notifyAll);
    otherBrowser.webProgress.addProgressListener(filter, notifyAll);
  }

  _swapBrowserDocShells(aOurTab, aOtherBrowser, aStateFlags) {
    // aOurTab's browser needs to be inserted now if it hasn't already.
    this._insertBrowser(aOurTab);

    // Unhook our progress listener
    const filter = this.#tabFilters.get(aOurTab);
    let tabListener = this.#tabListeners.get(aOurTab);
    let ourBrowser = this.getBrowserForTab(aOurTab);
    ourBrowser.webProgress.removeProgressListener(filter);
    filter.removeProgressListener(tabListener);

    // Make sure to unregister any open URIs.
    this._swapRegisteredOpenURIs(ourBrowser, aOtherBrowser);

    let remoteBrowser = aOtherBrowser.documentGlobal.gBrowser;

    // If switcher is active, it will intercept swap events and
    // react as needed.
    if (!this._switcher) {
      aOtherBrowser.docShellIsActive = this.shouldActivateDocShell(ourBrowser);
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
    this.#tabListeners.set(aOurTab, tabListener);

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
    lazy.SitePermissions.clearTemporaryBlockPermissions(browser);
    // Also reset DOS mitigations for the basic auth prompt on reload.
    delete browser.authPromptAbuseCounter;
    this.documentGlobal.gIdentityHandler.hidePopup();
    this.documentGlobal.gPermissionPanel.hidePopup();
    browser.reload();
  }

  /**
   * Adds a listener for web progress notifications about the selected browser.
   *
   * The listener is a plain object whose methods mirror
   * `nsIWebProgressListener`; implementing only the ones you need is normal.
   * `docs/progress-listeners.md` covers what is delivered when, and what a tab
   * switch replays.
   *
   * @param {object} aListener
   *   The listener to add.
   */
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

    this.#progressListeners.push(aListener);
  }

  /**
   * Removes a listener added with `addProgressListener`.
   *
   * @param {object} aListener
   *   The listener to remove.
   */
  removeProgressListener(aListener) {
    this.#progressListeners = this.#progressListeners.filter(
      l => l != aListener
    );
  }

  /**
   * Adds a listener for web progress notifications about every tab.
   *
   * As `addProgressListener`, except that each method takes the browser the
   * notification belongs to as an extra leading argument.
   *
   * @param {object} aListener
   *   The listener to add.
   */
  addTabsProgressListener(aListener) {
    this.#tabsProgressListeners.push(aListener);
  }

  /**
   * Removes a listener added with `addTabsProgressListener`.
   *
   * @param {object} aListener
   *   The listener to remove.
   */
  removeTabsProgressListener(aListener) {
    this.#tabsProgressListeners = this.#tabsProgressListeners.filter(
      l => l != aListener
    );
  }

  getBrowserForTab(aTab) {
    return aTab.linkedBrowser;
  }

  showTab(aTab) {
    if (!aTab.hidden || aTab == this.documentGlobal.FirefoxViewHandler.tab) {
      return;
    }
    aTab.removeAttribute("hidden");
    this.tabContainer._invalidateCachedVisibleTabs();

    this.tabContainer._updateCloseButtons();
    if (aTab.multiselected) {
      this._updateMultiselectedTabCloseButtonTooltip();
    }

    let event = this.document.createEvent("Events");
    event.initEvent("TabShow", true, false);
    aTab.dispatchEvent(event);
    lazy.SessionStore.deleteCustomTabValue(aTab, "hiddenBy");

    // Treat split view as one unit. Showing one of its tabs shows
    // the whole view. This prevents invisible tabs from appearing in the
    // splitview container.
    if (aTab.splitview) {
      for (let sibling of aTab.splitview.tabs) {
        if (sibling != aTab) {
          this.showTab(sibling);
        }
      }
    }
  }

  hideTab(aTab, aSource) {
    if (
      aTab.hidden ||
      aTab.pinned ||
      aTab.selected ||
      aTab.closing ||
      // Tabs that are sharing the screen, microphone or camera cannot be hidden.
      aTab.linkedBrowser._sharingState?.webRTC?.sharing
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

    let event = this.document.createEvent("Events");
    event.initEvent("TabHide", true, false);
    aTab.dispatchEvent(event);
    if (aSource) {
      lazy.SessionStore.setCustomTabValue(aTab, "hiddenBy", aSource);
    }

    // Treat split view as one unit. Hiding one of its tabs hides the whole
    // view.
    // One exception: a selected tab can't be hidden (see the
    // guard above), so if the split view holds the active tab, that tab
    // stays selected while its sibling is set to hidden. It remains selected
    // until focus moves to a tab outside the view. Despite a mismatch in
    // state, the splitview container still becomes visually hidden via a
    // display: none rule in browser/themes/shared/tabbrowser/tabs.css.
    if (aTab.splitview) {
      for (let sibling of aTab.splitview.tabs) {
        if (sibling != aTab) {
          this.hideTab(sibling, aSource);
        }
      }
    }
  }

  selectTabAtIndex(aIndex, { event, metricsContext } = {}) {
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

    this.setSelectedTab(tabs[aIndex], metricsContext);

    if (event) {
      event.preventDefault();
      event.stopPropagation();
    }
  }

  /**
   * Moves a tab to a new browser window, unless it's already the only tab
   * in the current window, in which case this will do nothing.
   *
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup|MozTabbrowserTabGroup.labelElement} aTab
   * @param {object} [aOptions={}]
   *   Key-value pairs that will be serialized into the features string.
   */
  replaceTabWithWindow(aTab, aOptions = {}) {
    if (this.tabs.length == 1) {
      return null;
    }
    // TODO bug 1967925: Consider handling the case where aTab is a tab group
    // and also the only tab group in its window.

    // Play the tab closing animation to give immediate feedback while
    // waiting for the new window to appear.
    if (!this.documentGlobal.gReduceMotion && this.isTab(aTab)) {
      aTab.style.maxWidth = ""; // ensure that fade-out transition happens
      aTab.removeAttribute("fadein");
    }

    // tell a new window to take the "dropped" tab
    let args = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
    args.appendElement(aTab.splitview ?? aTab);
    return lazy.BrowserWindowTracker.openWindow({
      private: lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal),
      features: Object.entries(aOptions)
        .map(([key, value]) => `${key}=${value}`)
        .join(","),
      openerWindow: this.documentGlobal,
      args,
    });
  }

  /**
   * Move contextTab (or selected tabs in a mutli-select context)
   * to a new browser window, unless it is (they are) already the only tab(s)
   * in the current window, in which case this will do nothing.
   *
   * @param {MozTabbrowserTab} contextTab
   *   The tab the command applies to, which need not be selected.
   * @param {object} [aOptions]
   *   Key-value pairs that will be serialized into the features string.
   */
  replaceTabsWithWindow(contextTab, aOptions = {}) {
    if (this.isTabGroupLabel(contextTab)) {
      // TODO bug 1967937: Pass contextTab.group instead.
      return this.replaceTabWithWindow(contextTab, aOptions);
    }

    let elements;
    if (contextTab.multiselected) {
      elements = this.selectedElements;
    } else {
      elements = [contextTab.splitview ?? contextTab];
    }

    if (this.tabs.length == elements.length) {
      return null;
    }

    this.recordTabMetrics(
      this.TabMetrics.METRIC_ACTION.DETACH,
      aOptions.metricsContext,
      { tabCount: elements.length }
    );

    if (elements.length == 1) {
      return this.replaceTabWithWindow(elements[0], aOptions);
    }

    // Play the closing animation for all selected tabs to give
    // immediate feedback while waiting for the new window to appear.
    if (!this.documentGlobal.gReduceMotion) {
      for (let element of elements) {
        element.style.maxWidth = ""; // ensure that fade-out transition happens
        element.removeAttribute("fadein");
      }
    }

    // Create a new window and make it adopt the tabs, preserving their relative order.
    // The initial tab of the new window will be selected, so it should adopt the
    // selected tab of the original window, if applicable, or else the first moving tab.
    // This avoids tab-switches in the new window, preserving tab laziness.
    // However, to avoid multiple tab-switches in the original window, the other tabs
    // should be adopted before the selected one.
    let { selectedTab } = this;
    if (
      !elements.includes(selectedTab) &&
      !elements.includes(selectedTab.splitview)
    ) {
      selectedTab = this.isSplitViewWrapper(elements[0])
        ? elements[0].tabs[0]
        : elements[0];
    }

    let win = this.replaceTabWithWindow(selectedTab, aOptions);
    win.addEventListener(
      "before-initial-tab-adopted",
      () => {
        let tabIndex = 0;
        for (let element of elements) {
          if (element !== selectedTab && element !== selectedTab.splitview) {
            const newTab = win.gBrowser.isSplitViewWrapper(element)
              ? win.gBrowser.adoptSplitView(element, {
                  elementIndex: tabIndex,
                })
              : win.gBrowser.adoptTab(element, { tabIndex });
            if (!newTab) {
              // The adoption failed. Restore "fadein" and don't increase the index.
              element.setAttribute("fadein", "true");
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
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  replaceGroupWithWindow(group, { metricsContext } = {}) {
    this.recordTabMetrics(
      this.TabMetrics.METRIC_ACTION.DETACH,
      metricsContext,
      { tabCount: group.tabs.length }
    );
    return this.replaceTabWithWindow(group);
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

  /**
   * @param {Element} element
   * @returns {boolean}
   *   `true` if element is a `<tab-split-view-wrapper>`
   */
  isSplitViewWrapper(element) {
    return !!(element?.tagName == "tab-split-view-wrapper");
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
   *   `MozTabbrowserTabs::dragAndDropElements` array.
   * @param {boolean} [options.forceUngrouped=false]
   *   Force `element` to move into position as a standalone tab, overriding
   *   any possibility of entering a tab group. For example, setting `true`
   *   ensures that a pinned tab will not accidentally be placed inside of
   *   a tab group, since pinned tabs are presently not allowed in tab groups.
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveTabTo(
    element,
    { elementIndex, tabIndex, forceUngrouped = false, metricsContext } = {}
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
    // When asked to move a tab in a splitview, move the entire wrapper instead.
    if (element.splitview) {
      element = element.splitview;
    }
    // When moving backwards, placing an element at tabIndex works trivially.
    // When moving forwards to a higher tabIndex, we need to increase the
    // index to account for the fact that the act of moving (multiple) tabs
    // causes all following tabs to have a decreased index.
    let movingForwards = false;
    if (this.isTab(element)) {
      movingForwards = tabIndex > element._tPos;
    } else {
      // tab group or split view (mutually exclusive with being pinned).
      let tabsInElement = element.tabs;
      movingForwards = tabIndex > tabsInElement[0]._tPos;
      if (movingForwards) {
        // -1 because element will be *after* tabIndex instead of at it.
        tabIndex += tabsInElement.length - 1;
        tabIndex = Math.min(tabIndex, this.tabs.length);
      }
    }
    this.#handleTabMove(
      element,
      () => {
        let neighbor = this.tabs[tabIndex];
        if (forceUngrouped && neighbor?.group) {
          neighbor = neighbor.group;
        }
        if (neighbor?.splitview) {
          neighbor = neighbor.splitview;
          if (neighbor === element) {
            // Already inside the same split view, don't move.
            // If you want to swap tabs, use splitview.reverseTabs() instead.
            return;
          }
        }

        if (movingForwards && neighbor) {
          neighbor.after(element);
        } else {
          this.tabContainer.insertBefore(element, neighbor);
        }
      },
      { metricsContext }
    );
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveTabBefore(element, targetElement, { metricsContext } = {}) {
    this.#moveTabNextTo(element, targetElement, true, { metricsContext });
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup[]} elements
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveTabsBefore(elements, targetElement, { metricsContext } = {}) {
    this.#moveTabsNextTo(elements, targetElement, true, { metricsContext });
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveTabAfter(element, targetElement, { metricsContext } = {}) {
    this.#moveTabNextTo(element, targetElement, false, { metricsContext });
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup[]} elements
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveTabsAfter(elements, targetElement, { metricsContext } = {}) {
    this.#moveTabsNextTo(elements, targetElement, false, { metricsContext });
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
   *   The tab or tab group to move. Also accepts a tab group label as a
   *   stand-in for its group.
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
   * @param {boolean} [moveBefore=false]
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  #moveTabNextTo(
    element,
    targetElement,
    moveBefore = false,
    { metricsContext } = {}
  ) {
    if (this.isTabGroupLabel(targetElement)) {
      targetElement = targetElement.group;
      if (!moveBefore && !targetElement.collapsed) {
        // Right after the tab group label = before the first tab in the tab group
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
      // If the caller asks to move an unpinned element next to a pinned
      // tab, move the unpinned element to be the first unpinned element
      // in the tab strip. Potential scenarios:
      // 1. Moving an unpinned tab and the first unpinned tab is ungrouped:
      //    move the unpinned tab right before the first unpinned tab.
      // 2. Moving an unpinned tab and the first unpinned tab is grouped:
      //    move the unpinned tab right before the tab group.
      // 3. Moving a tab group and the first unpinned tab is ungrouped:
      //    move the tab group right before the first unpinned tab.
      // 4. Moving a tab group and the first unpinned tab is grouped:
      //    move the tab group right before the first unpinned tab's tab group.
      targetElement = this.tabs[this.pinnedTabCount];
      if (targetElement.group) {
        targetElement = targetElement.group;
      }
      moveBefore = true;
    }

    // We want to include the splitview wrapper if it's the targetElement, but
    // not in the case where we want to reverse tabs within the same splitview.
    if (targetElement?.splitview && !element.splitview) {
      targetElement = targetElement.splitview;
    }

    let getContainer = () =>
      element.pinned
        ? this.tabContainer.pinnedTabsContainer
        : this.tabContainer;

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
      { metricsContext }
    );
  }

  /**
   * @param {MozTabbrowserTab[]} elements
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
   * @param {boolean} [moveBefore=false]
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  #moveTabsNextTo(
    elements,
    targetElement,
    moveBefore = false,
    { metricsContext } = {}
  ) {
    this.recordTabMetrics(this.TabMetrics.METRIC_ACTION.MOVE, metricsContext, {
      tabCount: elements.length,
    });

    this.#moveTabNextTo(elements[0], targetElement, moveBefore, {
      metricsContext: this.TabMetrics.decomposedContext(metricsContext),
    });
    for (let i = 1; i < elements.length; i++) {
      this.#moveTabNextTo(elements[i], elements[i - 1], false, {
        metricsContext: this.TabMetrics.decomposedContext(metricsContext),
      });
    }
  }

  /**
   *
   * @param {MozTabbrowserTab} aTab
   * @param {MozTabSplitViewWrapper} aSplitViewWrapper
   * @param {int} [insertAtIndex=-1] An optional index for a tab to insert into the split view
   */
  moveTabToSplitView(aTab, aSplitViewWrapper, insertAtIndex = -1) {
    if (!this.isTab(aTab)) {
      throw new Error("Can only move a tab into a split view wrapper");
    }
    if (aTab.pinned) {
      return;
    }
    if (
      aTab.splitview &&
      aTab.splitview.splitViewId === aSplitViewWrapper.splitViewId
    ) {
      return;
    }

    this.#handleTabMove(aTab, () =>
      insertAtIndex > -1
        ? aSplitViewWrapper.insertBefore(
            aTab,
            aSplitViewWrapper.tabs[insertAtIndex]
          )
        : aSplitViewWrapper.appendChild(aTab)
    );
    this.removeFromMultiSelectedTabs(aTab);
    this.tabContainer._notifyBackgroundTab(aTab);
  }

  /**
   *
   * @param {MozTabbrowserTab} aTab
   * @param {MozTabbrowserTabGroup} aGroup
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveTabToExistingGroup(aTab, aGroup, { metricsContext } = {}) {
    if (!this.isTab(aTab)) {
      throw new Error("Can only move a tab into a tab group");
    }
    if (aTab.pinned) {
      return;
    }
    if (aTab.group && aTab.group.id === aGroup.id) {
      return;
    }
    if (aTab.splitview) {
      let splitViewTabs = aTab.splitview.tabs;
      this.#handleTabMove(
        aTab.splitview,
        () => aGroup.appendChild(aTab.splitview),
        { metricsContext }
      );
      for (const splitViewTab of splitViewTabs) {
        this.removeFromMultiSelectedTabs(splitViewTab);
        this.tabContainer._notifyBackgroundTab(splitViewTab);
      }
    } else {
      this.#handleTabMove(aTab, () => aGroup.appendChild(aTab), {
        metricsContext,
      });
      this.removeFromMultiSelectedTabs(aTab);
      this.tabContainer._notifyBackgroundTab(aTab);
    }
  }

  /**
   *
   * @param {MozSplitViewWrapper} aSplitView
   * @param {MozTabbrowserTabGroup} aGroup
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  moveSplitViewToExistingGroup(
    aSplitView,
    aGroup,
    { metricsContext = null } = {}
  ) {
    if (!this.isSplitViewWrapper(aSplitView)) {
      throw new Error("Can only move a split view into a tab group");
    }
    if (aSplitView.group && aSplitView.group.id === aGroup.id) {
      return;
    }

    let splitViewTabs = aSplitView.tabs;
    this.#handleTabMove(aSplitView, () => aGroup.appendChild(aSplitView), {
      metricsContext,
    });
    for (const splitViewTab of splitViewTabs) {
      this.removeFromMultiSelectedTabs(splitViewTab);
      this.tabContainer._notifyBackgroundTab(splitViewTab);
    }
  }

  /**
   * @typedef {object} TabMoveState
   * @property {number} tabIndex
   *   The tab's position within the `tabs` array.
   * @property {number} [elementIndex]
   *   Its position within the `MozTabbrowserTabs::dragAndDropElements` array,
   *   for a visible tab.
   * @property {string} [tabGroupId]
   *   ID of the tab group the tab is in, if any.
   * @property {number} [splitViewId]
   *   ID of the split view the tab is in, if any.
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
    if (tab.splitview) {
      state.splitViewId = tab.splitview.splitViewId;
    }
    return state;
  }

  /**
   * @param {MozTabbrowserTab} tab
   * @param {TabMoveState} [previousTabState]
   * @param {TabMoveState} [currentTabState]
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  #notifyOnTabMove(
    tab,
    previousTabState,
    currentTabState,
    { metricsContext } = {}
  ) {
    if (!this.isTab(tab) || !previousTabState || !currentTabState) {
      return;
    }

    let changedPosition = previousTabState.tabIndex != currentTabState.tabIndex;
    let changedTabGroup =
      previousTabState.tabGroupId != currentTabState.tabGroupId;
    let changedSplitView =
      previousTabState.splitViewId != currentTabState.splitViewId;

    if (changedPosition || changedTabGroup || changedSplitView) {
      tab.dispatchEvent(
        new this.documentGlobal.CustomEvent("TabMove", {
          bubbles: true,
          detail: {
            previousTabState,
            currentTabState,
            metricsContext: metricsContext ?? this.TabMetrics.UNKNOWN_CONTEXT,
          },
        })
      );

      this.recordTabMetrics(
        this.TabMetrics.METRIC_ACTION.MOVE,
        metricsContext,
        { tabCount: 1 }
      );
    }
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup|MozTabSplitViewWrapper} element
   * @param {function():void} moveActionCallback
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  handleTabMove(element, moveActionCallback, { metricsContext } = {}) {
    this.#handleTabMove(element, moveActionCallback, { metricsContext });
  }

  /**
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup|MozTabSplitViewWrapper} element
   * @param {function():void} moveActionCallback
   * @param {object} [options]
   * @param {TabMetricsContext} [options.metricsContext]
   *   The context for the operation for telemetry purposes.
   */
  #handleTabMove(element, moveActionCallback, { metricsContext } = {}) {
    let tabs;
    // TODO bug 2024173: consider removing element.splitview check.
    if (this.isTab(element) && element.splitview?.shouldMoveAllTabsAtOnce) {
      tabs = element.splitview.tabs;
    } else if (this.isTab(element)) {
      tabs = [element];
    } else if (this.isTabGroup(element) || this.isSplitViewWrapper(element)) {
      tabs = element.tabs;
    } else {
      throw new Error(
        "Can only move a tab, tab group, or split view within the tab bar"
      );
    }

    let wasFocused = this.document.activeElement == this.selectedTab;
    let previousTabStates = tabs.map(tab => this.#getTabMoveState(tab));

    moveActionCallback();

    // Clear tabs cache after moving nodes because the order of tabs may have
    // changed.
    this.tabContainer._invalidateCachedTabs();
    this.#lastRelatedTabMap = new WeakMap();
    this._updateTabsAfterInsert();

    if (wasFocused) {
      this.selectedTab.focus();
    }

    // When multiple tabs are moved forwards (tab group, split view), reverse
    // the order of TabMove, so that the index in previousTabState values are
    // still accurate until the event is dispatched. If we were to start with
    // the front tab, then logically that tab moves, and all following tabs
    // would shift, which would invalidate the index in previousTabState.
    let reverseEvents =
      tabs.length > 1 && tabs[0]._tPos > previousTabStates[0].tabIndex;

    for (let i = 0; i < tabs.length; i++) {
      let ii = reverseEvents ? tabs.length - i - 1 : i;
      let tab = tabs[ii];
      if (tab.selected) {
        this.tabContainer._handleTabSelect(true);
      }

      let currentTabState = this.#getTabMoveState(tab);
      this.#notifyOnTabMove(tab, previousTabStates[ii], currentTabState, {
        metricsContext,
      });
    }

    let currentFirst = this.#getTabMoveState(tabs[0]);
    if (
      this.isTabGroup(element) &&
      previousTabStates[0].tabIndex != currentFirst.tabIndex
    ) {
      let event = new this.documentGlobal.CustomEvent("TabGroupMoved", {
        bubbles: true,
      });
      element.dispatchEvent(event);
    }
  }

  /**
   * Adopts a tab from another browser window, and inserts it at the given index.
   *
   * @param {MozTabbrowserTab} aTab
   *   The tab to adopt, which belongs to another window.
   * @param {object} [options]
   * @param {number} [options.elementIndex]
   *   The desired position, expressed as the index within the
   *   `MozTabbrowserTabs::dragAndDropElements` array.
   * @param {number} [options.tabIndex]
   *   The desired position, expressed as the index within the `tabs` array.
   * @param {boolean} [options.selectTab=false]
   *   Whether to make the adopted tab the new active tab.
   * @returns {object}
   *    The new tab in the current window, null if the tab couldn't be adopted.
   */
  adoptTab(aTab, { elementIndex, tabIndex, selectTab = false } = {}) {
    // Swap the dropped tab with a new one we create and then close
    // it in the other window (making it seem to have moved between
    // windows). We also ensure that the tab we create to swap into has
    // the same remote type and process as the one we're swapping in.
    // This makes sure we don't get a short-lived process for the new tab.
    let linkedBrowser = aTab.linkedBrowser;
    let createLazyBrowser = !aTab.linkedPanel;
    let index;
    let nextElement;
    if (typeof elementIndex == "number") {
      index = elementIndex;
      nextElement = this.tabContainer.dragAndDropElements.at(elementIndex);
    } else {
      index = tabIndex;
      nextElement = this.tabs.at(tabIndex);
    }
    let tabInGroup = !!aTab.group;
    let params = {
      eventDetail: { adoptedTab: aTab },
      preferredRemoteType: linkedBrowser.remoteType,
      initialBrowsingContextGroupId: linkedBrowser.browsingContext?.group.id,
      skipAnimation: true,
      elementIndex,
      tabIndex,
      tabGroup: this.isTab(nextElement) && nextElement.group,
      createLazyBrowser,
    };

    // We want to explicitly set this param rather than carry it over to
    // avoid situations like an unpinned tab being dragged between pinned
    // tabs but not getting pinned as expected.
    let numPinned = this.pinnedTabCount;
    if (index < numPinned || (aTab.pinned && index == numPinned)) {
      params.pinned = true;
    }

    if (aTab.hasAttribute("usercontextid")) {
      // new tab must have the same usercontextid as the old one
      params.userContextId = aTab.getAttribute("usercontextid");
    }
    params.skipLoad = true;
    let newTab = this.addWebTab("about:blank", params);

    aTab.container.tabDragAndDrop.finishAnimateTabMove();

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

  moveTabForward({ metricsContext } = {}) {
    let { selectedTab } = this;
    let selectedTabOrSplitview = selectedTab.splitview || selectedTab;
    let nextTab = this.tabContainer.findNextTab(
      selectedTab.splitview?.lastElementChild || selectedTab,
      {
        direction: DIRECTION_FORWARD,
        filter: tab => !tab.hidden && selectedTab.pinned == tab.pinned,
      }
    );
    let nextTabOrSplitview = nextTab?.splitview || nextTab;
    if (nextTab) {
      this.#handleTabMove(
        selectedTab,
        () => {
          if (!selectedTab.group && nextTab.group) {
            if (nextTabOrSplitview.group.collapsed) {
              // Skip over collapsed tab group.
              nextTabOrSplitview.group.after(selectedTabOrSplitview);
            } else {
              // Enter first position of tab group.
              nextTabOrSplitview.group.insertBefore(
                selectedTabOrSplitview,
                nextTabOrSplitview
              );
            }
          } else if (selectedTab.group != nextTab.group) {
            // Standalone tab after tab group.
            selectedTab.group.after(selectedTabOrSplitview);
          } else {
            nextTabOrSplitview.after(selectedTabOrSplitview);
          }
        },
        { metricsContext }
      );
    } else if (selectedTab.group) {
      // selectedTab is the last tab and is grouped.
      // remove it from its group.
      selectedTab.group.after(selectedTabOrSplitview);
    }
  }

  moveTabBackward({ metricsContext } = {}) {
    let { selectedTab } = this;
    let selectedTabOrSplitview = selectedTab.splitview || selectedTab;
    let previousTab = this.tabContainer.findNextTab(
      selectedTab.splitview?.firstElementChild || selectedTab,
      {
        direction: DIRECTION_BACKWARD,
        filter: tab => !tab.hidden && selectedTab.pinned == tab.pinned,
      }
    );
    let previousTabOrSplitview = previousTab?.splitview || previousTab;
    if (previousTab) {
      this.#handleTabMove(
        selectedTab,
        () => {
          if (!selectedTab.group && previousTab.group) {
            if (previousTab.group.collapsed) {
              // Skip over collapsed tab group.
              previousTab.group.before(selectedTabOrSplitview);
            } else {
              // Enter last position of tab group.
              previousTab.group.append(selectedTabOrSplitview);
            }
          } else if (selectedTab.group != previousTab.group) {
            // Standalone tab before tab group.
            selectedTab.group.before(selectedTabOrSplitview);
          } else {
            previousTabOrSplitview.before(selectedTabOrSplitview);
          }
        },
        { metricsContext }
      );
    } else if (selectedTab.group) {
      // selectedTab is the first tab and is grouped.
      // remove it from its group.
      selectedTab.group.before(selectedTabOrSplitview);
    }
  }

  moveTabToStart(aTab = this.selectedTab, { metricsContext } = {}) {
    this.moveTabTo(aTab, {
      tabIndex: 0,
      forceUngrouped: true,
      metricsContext,
    });
  }

  moveTabToEnd(aTab = this.selectedTab, { metricsContext } = {}) {
    this.moveTabTo(aTab, {
      tabIndex: this.tabs.length - 1,
      forceUngrouped: true,
      metricsContext,
    });
  }

  /**
   * @param   {MozTabbrowserTab} aTab
   *          Can be from a different window as well
   * @param   {boolean} aRestoreTabImmediately
   *          Can defer loading of the tab contents
   * @param   {object} [aOptions]
   *          Takes `inBackground` and `tabIndex`, as
   *          `SessionStore.duplicateTab` does
   * @returns {MozTabbrowserTab}
   */
  duplicateTab(aTab, aRestoreTabImmediately, aOptions) {
    let newTab = lazy.SessionStore.duplicateTab(
      this.documentGlobal,
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
   *
   * @param {Set<MozTabbrowserTab>} [aTabsRemovedFromMultiselection]
   *          An optional Set of tabs that used to be (multi) selected, but
   *          no longer. The accessible name of their close button will be
   *          reset to default.
   */
  _updateMultiselectedTabCloseButtonTooltip(aTabsRemovedFromMultiselection) {
    const { selectedTabs } = this;
    const args = { tabCount: selectedTabs.length };
    selectedTabs.forEach(selectedTab => {
      this.document.l10n.setArgs(
        selectedTab.querySelector(".tab-close-button"),
        args
      );
    });
    args.tabCount = 1;
    aTabsRemovedFromMultiselection?.forEach(unselectedTab => {
      this.document.l10n.setArgs(
        unselectedTab.querySelector(".tab-close-button"),
        args
      );
    });
  }

  /**
   * Adds a tab into the (multi) selected tabs collection.
   *
   * Warning: this function can be called from a loop, when selecting several tabs.
   * Instead of adding expensive logic here, do it in `_endMultiSelectChange()`.
   *
   * @param {MozTabbrowserTab} aTab
   */
  addToMultiSelectedTabs(aTab) {
    if (this.isSplitViewWrapper(aTab)) {
      for (let tab of aTab.tabs) {
        this.addToMultiSelectedTabs(tab);
      }
      return;
    }

    if (aTab.splitview) {
      aTab.splitview.setAttribute("multiselected", "true");
      aTab.splitview.setAttribute("aria-selected", "true");
    }

    if (aTab.multiselected) {
      return;
    }

    aTab.setAttribute("multiselected", "true");
    aTab.setAttribute("aria-selected", "true");
    this._multiSelectedTabsSet.add(aTab);
    this._startMultiSelectChange();
    if (!this.#multiSelectChangeRemovals.delete(aTab)) {
      this.#multiSelectChangeAdditions.add(aTab);
    }
  }

  /**
   * Adds two given tabs and all tabs between them into the (multi) selected tabs collection
   *
   * @param {MozTabbrowserTab} aTab1
   * @param {MozTabbrowserTab} aTab2
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
  }

  /**
   * Removes a tab from the (multi) selected tabs collection.
   *
   * Warning: this function can be called from a loop, when unselecting several tabs.
   * Instead of adding expensive logic here, do it in `_endMultiSelectChange()`.
   *
   * @param {MozTabbrowserTab} aTab
   */
  removeFromMultiSelectedTabs(aTab) {
    if (!aTab.multiselected) {
      return;
    }

    if (aTab.splitview) {
      aTab.splitview.removeAttribute("multiselected");
      aTab.splitview.removeAttribute("aria-selected");
    }

    aTab.removeAttribute("multiselected");
    aTab.removeAttribute("aria-selected");
    this._multiSelectedTabsSet.delete(aTab);
    this._startMultiSelectChange();
    if (!this.#multiSelectChangeAdditions.delete(aTab)) {
      this.#multiSelectChangeRemovals.add(aTab);
    }
  }

  clearMultiSelectedTabs() {
    if (this.#clearMultiSelectionLocked) {
      if (this.#clearMultiSelectionLockedOnce) {
        this.#clearMultiSelectionLockedOnce = false;
        this.#clearMultiSelectionLocked = false;
      }
      return;
    }

    if (this.multiSelectedTabsCount < 1) {
      return;
    }

    for (let tab of this.selectedTabs) {
      this.removeFromMultiSelectedTabs(tab);
    }
    this.#lastMultiSelectedTabRef = null;
  }

  selectAllTabs() {
    let visibleTabs = this.visibleTabs;
    this.addRangeToMultiSelectedTabs(
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
    this.#clearMultiSelectionLockedOnce = true;
    this.#clearMultiSelectionLocked = true;
  }

  unlockClearMultiSelection() {
    this.#clearMultiSelectionLockedOnce = false;
    this.#clearMultiSelectionLocked = false;
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
    this.#clearMultiSelectionLocked = true;

    // Guarantee that #clearMultiSelectionLocked lock gets released.
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

    this.#clearMultiSelectionLocked = false;
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

  /**
   * For multiselection, splitsviews can also be selected alongside tabs.
   * The getter for selectedTabs returns an array with the splitview child tabs
   * rather than the splitview itself. Hence the need for selectedElements.
   * We need to know the indices of the draggable elements, such as the
   * splitview parent element. This getter returns an array of multiselected
   * tabs and splitview parent elements (as opposed to splitview child tabs).
   */
  get selectedElements() {
    let selectedElements = new Set();
    for (let selectedTab of this.selectedTabs) {
      selectedElements.add(selectedTab.splitview ?? selectedTab);
    }
    return Array.from(selectedElements.values());
  }

  get multiSelectedTabsCount() {
    return ChromeUtils.nondeterministicGetWeakSetKeys(
      this._multiSelectedTabsSet
    ).filter(this._mayTabBeMultiselected).length;
  }

  get lastMultiSelectedTab() {
    let tab = this.#lastMultiSelectedTabRef
      ? this.#lastMultiSelectedTabRef.get()
      : null;
    if (tab && tab.isConnected && this._multiSelectedTabsSet.has(tab)) {
      return tab;
    }
    let selectedTab = this.selectedTab;
    this.lastMultiSelectedTab = selectedTab;
    return selectedTab;
  }

  set lastMultiSelectedTab(aTab) {
    this.#lastMultiSelectedTabRef = Cu.getWeakReference(aTab);
  }

  _mayTabBeMultiselected(aTab) {
    return aTab.visible;
  }

  _startMultiSelectChange() {
    if (!this.#multiSelectChangeStarted) {
      this.#multiSelectChangeStarted = true;
      Promise.resolve().then(() => this._endMultiSelectChange());
    }
  }

  _endMultiSelectChange() {
    let noticeable = false;
    let { selectedTab } = this;
    if (this.#multiSelectChangeAdditions.size) {
      if (!selectedTab.multiselected) {
        this.addToMultiSelectedTabs(selectedTab);
      }
      noticeable = true;
    }
    if (this.#multiSelectChangeRemovals.size) {
      if (this.#multiSelectChangeRemovals.has(selectedTab)) {
        this._switchToNextMultiSelectedTab();
      }
      this._avoidSingleSelectedTab();
      noticeable = true;
    }
    if (noticeable) {
      this._updateMultiselectedTabCloseButtonTooltip(
        this.#multiSelectChangeRemovals
      );
    }
    this.#multiSelectChangeStarted = false;
    if (noticeable || this.#multiSelectChangeSelected) {
      this.#multiSelectChangeSelected = false;
      this.#multiSelectChangeAdditions.clear();
      this.#multiSelectChangeRemovals.clear();
      this.dispatchEvent(
        new this.documentGlobal.CustomEvent("TabMultiSelect", {
          bubbles: true,
        })
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

  pinMultiSelectedTabs({
    metricsContext = this.TabMetrics.UNKNOWN_CONTEXT,
  } = {}) {
    let tabs = this.selectedTabs;
    this.recordTabMetrics(this.TabMetrics.METRIC_ACTION.PIN, metricsContext, {
      tabCount: tabs.length,
    });
    for (let tab of tabs) {
      this.pinTab(tab, this.TabMetrics.decomposedContext(metricsContext));
    }
  }

  unpinMultiSelectedTabs({ metricsContext } = {}) {
    // The selectedTabs getter returns the tabs
    // in visual order. We need to unpin in reverse
    // order to maintain visual order.
    let selectedTabs = this.selectedTabs;
    this.recordTabMetrics(this.TabMetrics.METRIC_ACTION.UNPIN, metricsContext, {
      tabCount: selectedTabs.length,
    });
    for (let i = selectedTabs.length - 1; i >= 0; i--) {
      this.unpinTab(selectedTabs[i], {
        metricsContext: this.TabMetrics.decomposedContext(metricsContext),
      });
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
   *
   * @param {MozBrowser} aBrowser
   */
  shouldActivateDocShell(aBrowser) {
    if (this._switcher) {
      return this._switcher.shouldActivateDocShell(aBrowser);
    }
    return (
      (aBrowser == this.selectedBrowser && !this.document.hidden) ||
      this._printPreviewBrowsers.has(aBrowser) ||
      this.PictureInPicture.isOriginatingBrowser(aBrowser) ||
      this.splitViewBrowsers.includes(aBrowser)
    );
  }

  _getSwitcher() {
    if (!this._switcher) {
      this._switcher = new this.AsyncTabSwitcher(this);
    }
    return this._switcher;
  }

  warmupTab(aTab) {
    if (this.documentGlobal.gMultiProcessBrowser) {
      this._getSwitcher().warmupTab(aTab);
    }
  }

  /**
   * _maybeRequestReplyFromRemoteContent may call
   * aEvent.requestReplyFromRemoteContent if necessary.
   *
   * @param {Event} aEvent    The handling event.
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

  on_keydown(aEvent) {
    if (!aEvent.isTrusted) {
      // Don't let untrusted events mess with tabs.
      return;
    }

    // Skip this only if something has explicitly cancelled it.
    if (aEvent.defaultCancelled) {
      return;
    }

    // Skip if chrome code has cancelled this or keyboard lock prevented
    if (aEvent.defaultPrevented) {
      return;
    }

    // We only want to request reply if this is an actual action.
    const action = lazy.ShortcutUtils.getSystemActionForEvent(aEvent);
    if (
      action != null &&
      this.KeyboardLockUtils.mustWaitForKeyboardLockRequestedReply(aEvent)
    ) {
      return;
    }

    // Don't check if the event was already consumed because tab
    // navigation should always work for better user experience.
    switch (action) {
      case lazy.ShortcutUtils.TOGGLE_CARET_BROWSING:
        this._maybeRequestReplyFromRemoteContent(aEvent);
        return;
      case lazy.ShortcutUtils.MOVE_TAB_BACKWARD:
        this.moveTabBackward({
          metricsContext: this.TabMetrics.userTriggeredContext(
            this.TabMetrics.METRIC_SOURCE.KEYBOARD
          ),
        });
        aEvent.preventDefault();
        return;
      case lazy.ShortcutUtils.MOVE_TAB_FORWARD:
        this.moveTabForward({
          metricsContext: this.TabMetrics.userTriggeredContext(
            this.TabMetrics.METRIC_SOURCE.KEYBOARD
          ),
        });
        aEvent.preventDefault();
        return;
      case lazy.ShortcutUtils.MOVE_TAB_TO_START:
      case lazy.ShortcutUtils.MOVE_TAB_TO_END: {
        let userIsInputtingText =
          this.documentGlobal.windowUtils.IMEStatus !=
          Ci.nsIDOMWindowUtils.IME_STATUS_DISABLED;
        if (aEvent.defaultPrevented || userIsInputtingText) {
          return;
        }
        if (
          lazy.ShortcutUtils.getSystemActionForEvent(aEvent) ==
          lazy.ShortcutUtils.MOVE_TAB_TO_START
        ) {
          this.moveTabToStart();
        } else {
          this.moveTabToEnd();
        }
        aEvent.preventDefault();
        return;
      }
      case lazy.ShortcutUtils.CLOSE_TAB:
        if (this.multiSelectedTabsCount) {
          this.removeMultiSelectedTabs({
            metricsContext: this.TabMetrics.userTriggeredContext(
              this.TabMetrics.METRIC_SOURCE.KEYBOARD
            ),
          });
        } else if (!this.selectedTab.pinned) {
          this.removeCurrentTab({
            animate: true,
            metricsContext: this.TabMetrics.userTriggeredContext(
              this.TabMetrics.METRIC_SOURCE.KEYBOARD
            ),
          });
        }
        aEvent.preventDefault();
    }
  }

  #toggleCaretBrowsing() {
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
          this.documentGlobal,
          title,
          message,
          // Make "No" the default:
          promptService.STD_YES_NO_BUTTONS | promptService.BUTTON_POS_1_DEFAULT,
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

  on_keypress(aEvent) {
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

    switch (
      lazy.ShortcutUtils.getSystemActionForEvent(aEvent, {
        rtl: this.documentGlobal.RTL_UI,
      })
    ) {
      case lazy.ShortcutUtils.TOGGLE_CARET_BROWSING:
        if (
          aEvent.defaultPrevented ||
          this._maybeRequestReplyFromRemoteContent(aEvent)
        ) {
          break;
        }
        this.#toggleCaretBrowsing();
        break;

      case lazy.ShortcutUtils.NEXT_TAB:
        if (AppConstants.platform == "macosx") {
          this.tabContainer.advanceSelectedTab(DIRECTION_FORWARD, true, aEvent);
          aEvent.preventDefault();
        }
        break;
      case lazy.ShortcutUtils.PREVIOUS_TAB:
        if (AppConstants.platform == "macosx") {
          this.tabContainer.advanceSelectedTab(
            DIRECTION_BACKWARD,
            true,
            aEvent
          );
          aEvent.preventDefault();
        }
        break;
    }
  }

  on_framefocusrequested(aEvent) {
    let tab = this.getTabForBrowser(aEvent.target);
    if (!tab || tab == this.selectedTab) {
      // Let the focus manager try to do its thing by not calling
      // preventDefault(). It will still raise the window if appropriate.
      return;
    }
    this.selectedTab = tab;
    this.documentGlobal.focus();
    aEvent.preventDefault();
  }

  on_visibilitychange() {
    if (!this._switcher) {
      const inactive = this.document.hidden;
      for (const browser of this.selectedBrowsers) {
        browser.preserveLayers(inactive);
        browser.docShellIsActive = !inactive;
      }
    }
  }

  on_TabGroupCollapse(aEvent) {
    aEvent.target.tabs.forEach(tab => {
      this.removeFromMultiSelectedTabs(tab);
    });
  }

  on_TabGroupCreateByUser(aEvent) {
    this.tabGroupMenu.openCreateModal(aEvent.target);
  }

  on_TabGrouped(aEvent) {
    let tab = aEvent.detail;
    let uri =
      tab.linkedBrowser?.registeredOpenURI || tab._originalRegisteredOpenURI;
    if (uri) {
      this.UrlbarProviderOpenTabs.unregisterOpenTab(
        uri.spec,
        tab.userContextId,
        null,
        lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
      );
      this.UrlbarProviderOpenTabs.registerOpenTab(
        uri.spec,
        tab.userContextId,
        tab.group?.id,
        lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
      );
    }
  }

  on_TabUngrouped(aEvent) {
    let tab = aEvent.detail;
    let uri =
      tab.linkedBrowser?.registeredOpenURI || tab._originalRegisteredOpenURI;
    if (uri) {
      // By the time the tab makes it to us it is already ungrouped, but
      // the original group is preserved in the event target.
      let originalGroup = aEvent.target;
      this.UrlbarProviderOpenTabs.unregisterOpenTab(
        uri.spec,
        tab.userContextId,
        originalGroup.id,
        lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
      );
      this.UrlbarProviderOpenTabs.registerOpenTab(
        uri.spec,
        tab.userContextId,
        null,
        lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
      );
    }
  }

  on_TabSplitViewActivate(aEvent) {
    this.#activeSplitView = aEvent.detail.splitview;
    this.#moveSplitViewNotificationBoxes(aEvent.detail.tabs);
  }

  on_TabSplitViewDeactivate(aEvent) {
    if (this.#activeSplitView === aEvent.detail.splitview) {
      this.#activeSplitView = null;
    }
    this.#moveSplitViewNotificationBoxes(aEvent.detail.tabs);
  }

  /**
   * Move existing notification boxes into or out of Split View
   * panel containers.
   *
   * @param {MozTabbrowserTab[]} tabs
   */
  #moveSplitViewNotificationBoxes(tabs) {
    for (const tab of tabs) {
      let notificationBox = this.readNotificationBox(tab.linkedBrowser);
      if (notificationBox?._stack) {
        this.#insertNotificationBox(tab.linkedBrowser, notificationBox._stack);
      }
    }
  }

  on_activate() {
    this.selectedTab.updateLastSeenActive();
  }

  on_deactivate() {
    this.selectedTab.updateLastSeenActive();
  }

  on_change() {
    // "(prefers-color-scheme: dark)" changed
    this.#maybeRefreshIcons();
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
    if (!tab?.linkedBrowser) {
      return [];
    }

    // Get the PIDs of the content process and remote subframe processes
    let [contentPid, ...framePids] = lazy.E10SUtils.getBrowserPids(
      tab.linkedBrowser,
      this.documentGlobal.gFissionBrowser
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

      if (this.SponsorProtection.isProtectedBrowser(tab.linkedBrowser)) {
        debugStringArray.push("[S]");
      }

      if (debugStringArray.length) {
        labelArray.push(debugStringArray.join(" "));
      }
    }

    // Add a line to the tooltip with additional tab context (e.g. container
    let containerName = tab.userContextId
      ? lazy.ContextualIdentityService.getUserContextLabel(tab.userContextId)
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
        const keyElem = this.document.getElementById("key_toggleMute");
        l10nArgs.shortcut = lazy.ShortcutUtils.prettifyShortcut(keyElem);
      } else if (tab.hasAttribute("activemedia-blocked")) {
        l10nId = "tabbrowser-unblock-tab-audio-tooltip";
      } else {
        l10nId = tab.linkedBrowser.audioMuted
          ? "tabbrowser-unmute-tab-audio-background-tooltip"
          : "tabbrowser-mute-tab-audio-background-tooltip";
      }
      tooltip.label = "";
      this.document.l10n.setAttributes(tooltip, l10nId, l10nArgs);
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
    let methodName = `on_${aEvent.type}`;
    if (methodName in this) {
      this[methodName](aEvent);
    } else {
      throw new Error(`Unexpected event ${aEvent.type}`);
    }
  }

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "contextual-identity-updated": {
        let identity = aSubject.wrappedJSObject;
        for (let tab of this.tabs) {
          if (tab.getAttribute("usercontextid") == identity.userContextId) {
            lazy.ContextualIdentityService.setTabStyle(tab);
          }
        }
        break;
      }
      case "intl:app-locales-changed": {
        this.#populateTitleCache();
        this.updateTitlebar();
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

    let outerID = this.documentGlobal.docShell.outerWindowID;

    // We want panel IDs to be globally unique, that's why we include the
    // window ID. We switched to a monotonic counter as Date.now() lead
    // to random failures because of colliding IDs.
    return "panel-" + outerID + "-" + ++this._uniquePanelIDCounter;
  }

  destroy() {
    this.tabContainer.destroy();
    Services.obs.removeObserver(this, "contextual-identity-updated");
    Services.obs.removeObserver(this, "intl:app-locales-changed");

    for (let tab of this.tabs) {
      let browser = tab.linkedBrowser;
      if (browser.registeredOpenURI) {
        let userContextId = browser.getAttribute("usercontextid") || 0;
        this.UrlbarProviderOpenTabs.unregisterOpenTab(
          browser.registeredOpenURI.spec,
          userContextId,
          tab.group?.id,
          lazy.PrivateBrowsingUtils.isWindowPrivate(this.documentGlobal)
        );
        delete browser.registeredOpenURI;
      }

      let filter = this.#tabFilters.get(tab);
      if (filter) {
        browser.webProgress.removeProgressListener(filter);

        let listener = this.#tabListeners.get(tab);
        if (listener) {
          filter.removeProgressListener(listener);
          listener.destroy();
        }

        this.#tabFilters.delete(tab);
        this.#tabListeners.delete(tab);
      }
    }

    this.document.removeEventListener("keydown", this, {
      mozSystemGroup: true,
    });
    if (AppConstants.platform == "macosx") {
      this.document.removeEventListener("keypress", this, {
        mozSystemGroup: true,
      });
    }
    this.document.removeEventListener("visibilitychange", this);
    this.documentGlobal.removeEventListener("framefocusrequested", this);
    this.documentGlobal.removeEventListener("activate", this);
    this.documentGlobal.removeEventListener("deactivate", this);

    if (this.documentGlobal.gMultiProcessBrowser) {
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

      if (this.tabs.length == 1 && this.#shouldCloseWindowWithLastTab) {
        // We already did PermitUnload in the content process
        // for this tab (the only one in the window). So we don't
        // need to do it again for any tabs.
        this.documentGlobal.skipNextCanClose = true;
        // In the parent-process browser case, the nsCloseEvent will actually take
        // care of tearing down the window, but we need to do this ourselves in the
        // content-process browser case. Doing so in both cases doesn't appear to
        // hurt.
        this.documentGlobal.close();
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

    this.addEventListener("BrowserLoadRetargeted", event => {
      this.maybeCloseTabForRetargetedLoad(event.target);
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
      if (!browser.contentTitle && browser.contentPrincipal.isSystemPrincipal) {
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
          this.documentGlobal.focus();
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
              tabPrompt.onNextPromptShowAllowFocusCheckboxFor(promptPrincipal);
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
          this.documentGlobal.gURLBar.setURI();
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
        lazy.TabCrashHandler.onSubFrameCrash(browser, event.childID);
        return;
      }

      // Preloaded browsers do not actually have any tabs. If one crashes,
      // it should be released and removed.
      if (browser === this.preloadedBrowser) {
        lazy.NewTabPagePreloading.removePreloadedBrowser(this.documentGlobal);
        return;
      }

      let isRestartRequiredCrash = event.type == "oop-browser-buildid-mismatch";

      let icon = browser.mIconURL;
      let tab = this.getTabForBrowser(browser);

      if (this.selectedBrowser == browser) {
        lazy.TabCrashHandler.onSelectedBrowserCrash(
          browser,
          isRestartRequiredCrash
        );
      } else {
        lazy.TabCrashHandler.onBackgroundBrowserCrash(
          browser,
          isRestartRequiredCrash
        );
      }

      tab.removeAttribute("soundplaying");
      this.setIcon(tab, icon);
    };

    this.addEventListener("oop-browser-crashed", onTabCrashed);
    this.addEventListener("oop-browser-buildid-mismatch", onTabCrashed);

    for (let tab of this.tabs) {
      tab.registerAudibleChangeHandler();
    }

    this.tabContainer.addEventListener("TabBrowserInserted", event => {
      event.target.registerAudibleChangeHandler();
    });

    // When a tab switches between remote and non-remote (e.g. about:blank →
    // a real page, or a process-switch navigation), a new BrowsingContext is
    // created and a new MediaController is attached to it. Re-register here
    // so the onaudiblechange handler always points to the current controller.
    this.tabContainer.addEventListener("TabRemotenessChange", event => {
      event.target.registerAudibleChangeHandler();
    });

    this.tabContainer.addEventListener("TabClose", event => {
      event.target.unregisterAudibleChangeHandler();
    });

    this.tabContainer.addEventListener("TabBrowserDiscarded", event => {
      event.target.unregisterAudibleChangeHandler();
    });

    // Closing a window tears its tabs down without firing TabClose, so the
    // per-tab MediaController listeners would otherwise outlive the window
    // (their closures keep the window alive until shutdown). Drop them all
    // when the window unloads.
    this.documentGlobal.addEventListener(
      "unload",
      () => {
        for (let tab of this.tabs) {
          tab.unregisterAudibleChangeHandler();
        }
      },
      { once: true }
    );

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

      lazy.SitePermissions.setForPrincipal(
        browser.contentPrincipal,
        "autoplay-media",
        lazy.SitePermissions.BLOCK,
        lazy.SitePermissions.SCOPE_GLOBAL,
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
      this.tabContainer.removeEventListener("mouseover", tabContextFTLInserter);
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
      let evt = this.document.createEvent("Events");
      evt.initEvent("BeforeTabRemotenessChange", true, false);
      tab.dispatchEvent(evt);

      // Unhook our progress listener.
      let filter = this.#tabFilters.get(tab);
      let oldListener = this.#tabListeners.get(tab);
      browser.webProgress.removeProgressListener(filter);
      filter.removeProgressListener(oldListener);
      let stateFlags = oldListener._stateFlags;
      let requestCount = oldListener._requestCount;

      // We'll be creating a new listener, so destroy the old one.
      oldListener.destroy();

      let oldUserTypedValue = browser.userTypedValue;
      let hadStartedLoad = browser.didStartLoadSinceLastUserTyping();

      let didChange = () => {
        browser.userTypedValue = oldUserTypedValue;
        if (hadStartedLoad) {
          browser.urlbarChangeTracker.startedLoad();
        }

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
        this.#tabListeners.set(tab, listener);
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

        evt = this.document.createEvent("Events");
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

    this.splitViewCommandSet.addEventListener("command", event => {
      const source = this.document
        .getElementById("split-view-menu")
        ?.getAttribute("data-trigger-source");
      switch (event.target.id) {
        case "splitViewCmd_separateTabs":
          this.#activeSplitView.unsplitTabs(`${source}_separate`);
          break;
        case "splitViewCmd_reverseTabs":
          this.#activeSplitView.reverseTabs(source);
          break;
        case "splitViewCmd_closeTabs":
          this.#activeSplitView.close(`${source}_close`);
          break;
      }
    });
  }

  translateTabContextMenu() {
    if (this._tabContextMenuTranslated) {
      return;
    }
    this.documentGlobal.MozXULElement.insertFTLIfNeeded(
      "browser/tabContextMenu.ftl"
    );
    // Un-lazify the l10n-ids now that the FTL file has been inserted.
    this.document
      .getElementById("tabContextMenu")
      .querySelectorAll("[data-lazy-l10n-id]")
      .forEach(el => {
        el.setAttribute("data-l10n-id", el.getAttribute("data-lazy-l10n-id"));
        el.removeAttribute("data-lazy-l10n-id");
      });
    this._tabContextMenuTranslated = true;
  }

  setSuccessor(aTab, successorTab) {
    if (aTab.documentGlobal != this.documentGlobal) {
      throw new Error("Cannot set the successor of another window's tab");
    }
    if (successorTab == aTab) {
      successorTab = null;
    }
    if (successorTab && successorTab.documentGlobal != this.documentGlobal) {
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
   *
   * @param {MozTabbrowserTab} aTab
   * @param {MozTabbrowserTab} aOtherTab
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
   *
   * @param {MozBrowser} aBrowser
   */
  _getTriggeringPrincipalFromHistory(aBrowser) {
    let sessionHistory = aBrowser?.browsingContext?.sessionHistory;
    if (!sessionHistory || !sessionHistory.index || sessionHistory.count == 0) {
      return undefined;
    }
    let currentEntry = sessionHistory.getEntryAtIndex(sessionHistory.index);
    let triggeringPrincipal = currentEntry?.triggeringPrincipal;
    return triggeringPrincipal;
  }

  clearRelatedTabs() {
    this.#lastRelatedTabMap = new WeakMap();
  }
}

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
    // Initialize _stateFlags to non-zero e.g. when creating a progress
    // listener for preloaded browsers as there was no progress listener
    // around when the content started loading. If the content didn't
    // quite finish loading yet, _stateFlags will very soon be overridden
    // with the correct value and end up at STATE_STOP again.
    if (aWasPreloadedBrowser) {
      stateFlags =
        Ci.nsIWebProgressListener.STATE_STOP |
        Ci.nsIWebProgressListener.STATE_IS_REQUEST;
    }

    this._tab = aTab;
    this._browser = aBrowser;
    this._blank = aStartsBlank;

    // cache flags for correct status UI update after tab switching
    this._stateFlags = stateFlags;
    this._status = 0;
    this._message = "";
    this._totalProgress = 0;

    // count of open requests (should always be 0 or 1)
    this._requestCount = aOrigRequestCount || 0;
  }

  get #documentGlobal() {
    return this._tab.documentGlobal;
  }

  get #tabbrowser() {
    return this._tab.documentGlobal.gBrowser;
  }

  destroy() {
    delete this._tab;
    delete this._browser;
  }

  _callProgressListeners(...args) {
    args.unshift(this._browser);
    return this.#tabbrowser._callProgressListeners(...args);
  }

  _shouldShowProgress(aRequest) {
    if (this._blank) {
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
    if (!this._blank || !aWebProgress.isTopLevel) {
      return false;
    }

    // If the state has STATE_STOP, and no requests were in flight, then this
    // must be the initial "stop" for the initial about:blank document.
    if (
      aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
      this._requestCount == 0 &&
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
    this._totalProgress = aMaxTotalProgress
      ? aCurTotalProgress / aMaxTotalProgress
      : 0;

    if (!this._shouldShowProgress(aRequest)) {
      return;
    }

    if (this._totalProgress && this._tab.hasAttribute("busy")) {
      this._tab.setAttribute("progress", "true");
      this.#tabbrowser._tabAttrModified(this._tab, ["progress"]);
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
      (!ignoreBlank && this._blank)
    ) {
      this._blank = false;
    }

    if (aStateFlags & STATE_START && aStateFlags & STATE_IS_NETWORK) {
      this._requestCount++;

      if (aWebProgress.isTopLevel) {
        // Need to use originalLocation rather than location because things
        // like about:home and about:privatebrowsing arrive with nsIRequest
        // pointing to their resolved jar: or file: URIs.
        if (
          !(
            originalLocation &&
            this.#documentGlobal.gInitialPages.includes(
              originalLocation.spec
            ) &&
            originalLocation != "about:blank" &&
            this._browser.initialPageLoadedFromUserAction !=
              originalLocation.spec &&
            this._browser.currentURI &&
            this._browser.currentURI.spec == "about:blank"
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
          this._browser.urlbarChangeTracker.startedLoad();

          // To improve the user experience and perceived performance when
          // opening links in new tabs, we show the url and tab title sooner,
          // but only if it's safe (from a phishing point of view) to do so,
          // thus there's no session history and the load starts from a
          // non-web-controlled blank page.
          if (
            this._browser.browsingContext.sessionHistory?.count === 0 &&
            (this._browser.initiatedFromNonWebControlled ||
              lazy.BrowserUIUtils.checkEmptyPageOrigin(
                this._browser,
                originalLocation
              ))
          ) {
            this.#tabbrowser.setInitialTabTitle(
              this._tab,
              originalLocation.spec,
              {
                isURL: true,
              }
            );

            this._browser.browsingContext.nonWebControlledLoadingURI =
              originalLocation;
            if (this._tab.selected && !this.#tabbrowser.userTypedValue) {
              this.#documentGlobal.gURLBar.setURI();
            }
          }
        }
        delete this._browser.initialPageLoadedFromUserAction;
        delete this._browser.initiatedFromNonWebControlled;
        // If the browser is loading it must not be crashed anymore
        this._tab.removeAttribute("crashed");
      }

      if (this._shouldShowProgress(aRequest)) {
        if (
          !(aStateFlags & Ci.nsIWebProgressListener.STATE_RESTORING) &&
          aWebProgress &&
          aWebProgress.isTopLevel
        ) {
          this._tab.setAttribute("busy", "true");
          this.#tabbrowser._tabAttrModified(this._tab, ["busy"]);
          this._tab._notselectedsinceload = !this._tab.selected;
        }

        if (this._tab.selected) {
          this.#tabbrowser._isBusy = true;
        }
      }
    } else if (aStateFlags & STATE_STOP && aStateFlags & STATE_IS_NETWORK) {
      // since we (try to) only handle STATE_STOP of the last request,
      // the count of open requests should now be 0
      this._requestCount = 0;

      let modifiedAttrs = [];
      if (this._tab.hasAttribute("busy")) {
        this._tab.removeAttribute("busy");
        modifiedAttrs.push("busy");

        // Only animate the "burst" indicating the page has loaded if
        // the top-level page is the one that finished loading.
        if (
          aWebProgress.isTopLevel &&
          !aWebProgress.isLoadingDocument &&
          Components.isSuccessCode(aStatus) &&
          !this.#tabbrowser.tabAnimationsInProgress &&
          !this.#documentGlobal.gReduceMotion
        ) {
          if (this._tab._notselectedsinceload) {
            this._tab.setAttribute("notselectedsinceload", "true");
          } else {
            this._tab.removeAttribute("notselectedsinceload");
          }

          this._tab.setAttribute("bursting", "true");
        }
      }

      if (this._tab.hasAttribute("progress")) {
        this._tab.removeAttribute("progress");
        modifiedAttrs.push("progress");
      }

      if (aWebProgress.isTopLevel) {
        let isSuccessful = Components.isSuccessCode(aStatus);
        if (!isSuccessful && !this._tab.isEmpty) {
          // Restore the current document's location in case the
          // request was stopped (possibly from a content script)
          // before the location changed.

          this._browser.userTypedValue = null;
          // When SHIP is enabled and a load gets cancelled due to another one
          // starting, the error is NS_BINDING_CANCELLED_OLD_LOAD.
          // When these prefs are not enabled, the error is different and
          // that's why we still want to look at the isNavigating flag.
          // We could add a workaround and make sure that in the alternative
          // codepaths we would also omit the same error, but considering
          // how we will be enabling fission by default soon, we can keep
          // using isNavigating for now, and remove it when SHIP is enabled
          // by default.
          // Bug 1725716 has been filed to consider removing isNavigating
          // field alltogether.
          let isNavigating = this._browser.isNavigating;
          if (
            this._tab.selected &&
            aStatus != Cr.NS_BINDING_CANCELLED_OLD_LOAD &&
            !isNavigating
          ) {
            this.#documentGlobal.gURLBar.setURI();
          }
        } else if (isSuccessful) {
          this._browser.urlbarChangeTracker.finishedLoad();
        }
      }

      // If we don't already have an icon for this tab then clear the tab's
      // icon. Don't do this on the initial about:blank load to prevent
      // flickering. Don't clear the icon if we already set it from one of the
      // known defaults. Note we use the original URL since about:newtab
      // redirects to a prerendered page.
      const shouldRemoveFavicon =
        !this._browser.mIconURL &&
        !ignoreBlank &&
        !(originalLocation.spec in FAVICON_DEFAULTS);
      if (shouldRemoveFavicon && this._tab.hasAttribute("image")) {
        this._tab.removeAttribute("image");
        modifiedAttrs.push("image");
      } else if (!shouldRemoveFavicon) {
        // Bug 1804166: Allow new tabs to set the favicon correctly if the
        // new tabs behavior is set to open a blank page.
        // This is a no-op unless this._browser.documentURI is in
        // FAVICON_DEFAULTS.
        this.#tabbrowser.setDefaultIcon(this._tab, this._browser.documentURI);
      }

      // For keyword URIs clear the user typed value since they will be changed into real URIs
      if (location.scheme == "keyword") {
        this._browser.userTypedValue = null;
      }

      if (this._tab.selected) {
        this.#tabbrowser._isBusy = false;
      }

      if (modifiedAttrs.length) {
        this.#tabbrowser._tabAttrModified(this._tab, modifiedAttrs);
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
      this._message = "";
      this._totalProgress = 0;
    }
    this._stateFlags = aStateFlags;
    this._status = aStatus;
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
        this._browser.didStartLoadSinceLastUserTyping() ||
        (isErrorPage && aLocation.spec != "about:blank") ||
        (isSameDocument && this._browser.isNavigating) ||
        (isSameDocument && !this._browser.userTypedValue)
      ) {
        this._browser.userTypedValue = null;
      }

      // If the tab has been set to "busy" outside the stateChange
      // handler below (e.g. by sessionStore.navigateAndRestore), and
      // the load results in an error page, it's possible that there
      // isn't any (STATE_IS_NETWORK & STATE_STOP) state to cause busy
      // attribute being removed. In this case we should remove the
      // attribute here.
      if (isErrorPage && this._tab.hasAttribute("busy")) {
        this._tab.removeAttribute("busy");
        this.#tabbrowser._tabAttrModified(this._tab, ["busy"]);
      }

      if (!isSameDocument) {
        // If the browser was playing audio, we should remove the playing state.
        if (this._tab.hasAttribute("soundplaying")) {
          this.#documentGlobal.clearTimeout(
            this._tab._soundPlayingAttrRemovalTimer
          );
          this._tab._soundPlayingAttrRemovalTimer = 0;
          this._tab.removeAttribute("soundplaying");
          this.#tabbrowser._tabAttrModified(this._tab, ["soundplaying"]);
        }

        // If the browser was previously muted, we should restore the muted state.
        if (this._tab.hasAttribute("muted")) {
          this._tab.linkedBrowser.browsingContext?.mediaController?.mute();
        }

        if (this.#tabbrowser.isFindBarInitialized(this._tab)) {
          let findBar = this.#tabbrowser.getCachedFindBar(this._tab);

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
          this.#tabbrowser.setTabTitle(this._tab);
        }

        // Don't clear the favicon if this tab is in the pending
        // state, as SessionStore will have set the icon for us even
        // though we're pointed at an about:blank. Also don't clear it
        // if the tab is in customize mode, to keep the one set by
        // gCustomizeMode.setTab (bug 1551239). Also don't clear it
        // if onLocationChange was triggered by a pushState or a
        // replaceState (bug 550565) or a hash change (bug 408415).
        if (
          !this._tab.hasAttribute("pending") &&
          !this._tab.hasAttribute("customizemode") &&
          aWebProgress.isLoadingDocument
        ) {
          // Removing the tab's image here causes flickering, wait until the
          // load is complete.
          this._browser.mIconURL = null;
        }

        if (!isReload && aWebProgress.isLoadingDocument) {
          let triggerer = this.#tabbrowser._getTriggeringPrincipalFromHistory(
            this._browser
          );
          // Typing a url, searching or clicking a bookmark will load a new
          // document that is no longer tied to a navigation from the previous
          // content and will have a system principal as the triggerer.
          if (triggerer && triggerer.isSystemPrincipal) {
            // Reset the related tab map so that the next tab opened will be related
            // to this new document and not to tabs opened by the previous one.
            this.#tabbrowser.clearRelatedTabs();
          }
        }

        if (
          aRequest instanceof Ci.nsIChannel &&
          !this.#documentGlobal.isBlankPageURL(aRequest.originalURI.spec)
        ) {
          this._browser.originalURI = aRequest.originalURI;
        }

        if (!this.#tabbrowser._allowTransparentBrowser) {
          this._browser.toggleAttribute(
            "transparent",
            lazy.AIWindow.isAIWindowActive(this.#documentGlobal) &&
              lazy.AIWindow.isAIWindowContentPage(aLocation)
          );
        }
      }

      let userContextId = this._browser.getAttribute("usercontextid") || 0;
      if (this._browser.registeredOpenURI) {
        let uri = this._browser.registeredOpenURI;
        this.#tabbrowser.UrlbarProviderOpenTabs.unregisterOpenTab(
          uri.spec,
          userContextId,
          this._tab.group?.id,
          lazy.PrivateBrowsingUtils.isWindowPrivate(this.#documentGlobal)
        );
        delete this._browser.registeredOpenURI;
      }
      if (!this.#documentGlobal.isBlankPageURL(aLocation.spec)) {
        this.#tabbrowser.UrlbarProviderOpenTabs.registerOpenTab(
          aLocation.spec,
          userContextId,
          this._tab.group?.id,
          lazy.PrivateBrowsingUtils.isWindowPrivate(this.#documentGlobal)
        );
        this._browser.registeredOpenURI = aLocation;

        // Record telemetry for URI loads in split view
        if (this._tab.splitview && aLocation.spec !== "about:opentabs") {
          const index = this._tab.splitview.tabs.indexOf(this._tab);
          const label = String(index + 1); // 0 -> "1" (LTR left), 1 -> "2" (LTR right)
          Glean.splitview.uriCount[label].add(1);
        }
      }

      if (this._tab != this.#tabbrowser.selectedTab) {
        let tabCacheIndex = this.#tabbrowser._tabLayerCache.indexOf(this._tab);
        if (tabCacheIndex != -1) {
          this.#tabbrowser._tabLayerCache.splice(tabCacheIndex, 1);
          this.#tabbrowser._getSwitcher().cleanUpTabAfterEviction(this._tab);
        }
      }
    }

    if (!this._blank || this._browser.hasContentOpener) {
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
      this._browser.lastURI = aLocation;
      this._browser.lastLocationChange = Date.now();
    }
  }

  onStatusChange(aWebProgress, aRequest, aStatus, aMessage) {
    if (this._blank) {
      return;
    }

    this._callProgressListeners("onStatusChange", [
      aWebProgress,
      aRequest,
      aStatus,
      aMessage,
    ]);

    this._message = aMessage;
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
      browser.ownerDocument.hasValidTransientUserGestureActivation;
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
    if (lazy.PrivateBrowsingUtils.isBrowserPrivate(browser)) {
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
      let fixupInfo = Services.uriFixup.getFixupURIInfo(uriString, fixupFlags);
      return fixupInfo.preferredURI;
    } catch (e) {
      // getFixupURIInfo may throw. Just return null, our caller will deal.
    }
    return null;
  },

  _updateTriggerMetadataForLoad(
    browser,
    uriString,
    { loadFlags, globalHistoryOptions }
  ) {
    let { SponsorProtection } = browser.getTabBrowser();
    if (globalHistoryOptions?.triggeringSponsoredURL) {
      if (globalHistoryOptions.triggeringSource == "newtab") {
        SponsorProtection.addProtectedBrowser(browser);
      }

      try {
        // Browser may access URL after fixing it up, then store the URL into DB.
        // To match with it, fix the link up explicitly.
        const triggeringSponsoredURL = Services.uriFixup.getFixupURIInfo(
          globalHistoryOptions.triggeringSponsoredURL,
          this._loadFlagsToFixupFlags(browser, loadFlags)
        ).fixedURI.spec;
        browser.setAttribute("triggeringSponsoredURL", triggeringSponsoredURL);
        const time =
          globalHistoryOptions.triggeringSponsoredURLVisitTimeMS || Date.now();
        browser.setAttribute("triggeringSponsoredURLVisitTimeMS", time);
        browser.setAttribute(
          "triggeringSource",
          globalHistoryOptions.triggeringSource
        );
      } catch (e) {}
    } else {
      SponsorProtection.removeProtectedBrowser(browser);
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

    this._updateTriggerMetadataForLoad(
      browser,
      uriString || uri.spec,
      loadURIOptions
    );

    if (loadURIOptions.isCaptivePortalTab) {
      browser.browsingContext.isCaptivePortalTab = true;
    }

    // XXX(nika): Is `browser.isNavigating` necessary anymore?
    // XXX(gijs): Unsure. But it mirrors docShell.isNavigating, but in the parent process
    // (and therefore imperfectly so).
    browser.isNavigating = true;

    try {
      // Should more generally prefer loadURI here - see bug 1815509.
      if (startedWithURI) {
        browser.webNavigation.loadURI(uri, loadURIOptions);
      } else {
        browser.webNavigation.fixupAndLoadURIString(uriString, loadURIOptions);
      }
    } finally {
      browser.isNavigating = false;
    }
  },
};
