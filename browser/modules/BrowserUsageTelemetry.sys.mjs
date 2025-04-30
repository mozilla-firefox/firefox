/* -*- js-indent-level: 2; indent-tabs-mode: nil -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  CustomizableUI: "resource:///modules/CustomizableUI.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  PageActions: "resource:///modules/PageActions.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchSERPTelemetry:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SearchSERPTelemetryUtils:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
  WindowsInstallsInfo:
    "resource://gre/modules/components-utils/WindowsInstallsInfo.sys.mjs",

  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// This pref is in seconds!
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gRecentVisitedOriginsExpiry",
  "browser.engagement.recent_visited_origins.expiry"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarVerticalTabs",
  "sidebar.verticalTabs",
  false,
  (_aPreference, _previousValue, isVertical) => {
    let tabCount = getOpenTabsAndWinsCounts().tabCount;
    BrowserUsageTelemetry.maxTabCount = tabCount;
    let pinnedTabCount = getPinnedTabsCount();
    BrowserUsageTelemetry.maxTabPinnedCount = pinnedTabCount;
    if (isVertical) {
      Glean.browserEngagement.maxConcurrentVerticalTabCount.set(tabCount);
      Glean.browserEngagement.maxConcurrentVerticalTabPinnedCount.set(
        pinnedTabCount
      );
    } else {
      Glean.browserEngagement.maxConcurrentTabCount.set(tabCount);
      Glean.browserEngagement.maxConcurrentTabPinnedCount.set(pinnedTabCount);
    }
  }
);

// The upper bound for the count of the visited unique domain names.
const MAX_UNIQUE_VISITED_DOMAINS = 100;

// Observed topic names.
const TAB_RESTORING_TOPIC = "SSTabRestoring";
const TELEMETRY_SUBSESSIONSPLIT_TOPIC =
  "internal-telemetry-after-subsession-split";
const DOMWINDOW_OPENED_TOPIC = "domwindowopened";
const SESSION_STORE_SAVED_TAB_GROUPS_TOPIC =
  "sessionstore-saved-tab-groups-changed";

export const MINIMUM_TAB_COUNT_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes, in ms

// The elements we consider to be interactive.
const UI_TARGET_ELEMENTS = [
  "menuitem",
  "toolbarbutton",
  "key",
  "command",
  "checkbox",
  "input",
  "button",
  "image",
  "radio",
  "richlistitem",
  "moz-checkbox",
];
const UI_TARGET_COMPOSED_ELEMENTS_MAP = new Map([["moz-checkbox", "input"]]);

// The containers of interactive elements that we care about and their pretty
// names. These should be listed in order of most-specific to least-specific,
// when iterating JavaScript will guarantee that ordering and so we will find
// the most specific area first.
const BROWSER_UI_CONTAINER_IDS = {
  "toolbar-menubar": "menu-bar",
  TabsToolbar: "tabs-bar",
  "vertical-tabs": "vertical-tabs-container",
  PersonalToolbar: "bookmarks-bar",
  "appMenu-popup": "app-menu",
  tabContextMenu: "tabs-context",
  contentAreaContextMenu: "content-context",
  "widget-overflow-list": "overflow-menu",
  "widget-overflow-fixed-list": "pinned-overflow-menu",
  "page-action-buttons": "pageaction-urlbar",
  pageActionPanel: "pageaction-panel",
  "unified-extensions-area": "unified-extensions-area",
  "allTabsMenu-allTabsView": "alltabs-menu",

  // This should appear last as some of the above are inside the nav bar.
  "nav-bar": "nav-bar",
};

const ENTRYPOINT_TRACKED_CONTEXT_MENU_IDS = {
  [BROWSER_UI_CONTAINER_IDS.tabContextMenu]: "tabs-context-entrypoint",
};

// A list of the expected panes in about:preferences
const PREFERENCES_PANES = [
  "paneHome",
  "paneGeneral",
  "panePrivacy",
  "paneSearch",
  "paneSearchResults",
  "paneSync",
  "paneContainers",
  "paneExperimental",
  "paneMoreFromMozilla",
];

const IGNORABLE_EVENTS = new WeakMap();

const KNOWN_ADDONS = [];

// Buttons that, when clicked, set a preference to true. The convention
// is that the preference is named:
//
// browser.engagement.<button id>.has-used
//
// and is defaulted to false.
const SET_USAGE_PREF_BUTTONS = [
  "downloads-button",
  "fxa-toolbar-menu-button",
  "home-button",
  "sidebar-button",
  "library-button",
];

// Buttons that, when clicked, increase a counter. The convention
// is that the preference is named:
//
// browser.engagement.<button id>.used-count
//
// and doesn't have a default value.
const SET_USAGECOUNT_PREF_BUTTONS = [
  "pageAction-panel-copyURL",
  "pageAction-panel-emailLink",
  "pageAction-panel-pinTab",
  "pageAction-panel-screenshots_mozilla_org",
  "pageAction-panel-shareURL",
];

// Places context menu IDs.
const PLACES_CONTEXT_MENU_ID = "placesContext";
const PLACES_OPEN_IN_CONTAINER_TAB_MENU_ID =
  "placesContext_open:newcontainertab";

// Commands used to open history or bookmark links from places context menu.
const PLACES_OPEN_COMMANDS = [
  "placesCmd_open",
  "placesCmd_open:window",
  "placesCmd_open:privatewindow",
  "placesCmd_open:tab",
];

// How long of a delay between events means the start of a new flow?
// Used by Browser UI Interaction event instrumentation.
// Default: 5min.
const FLOW_IDLE_TIME = 5 * 60 * 1000;

function telemetryId(widgetId, obscureAddons = true) {
  // Add-on IDs need to be obscured.
  function addonId(id) {
    if (!obscureAddons) {
      return id;
    }

    let pos = KNOWN_ADDONS.indexOf(id);
    if (pos < 0) {
      pos = KNOWN_ADDONS.length;
      KNOWN_ADDONS.push(id);
    }
    return `addon${pos}`;
  }

  if (widgetId.endsWith("-browser-action")) {
    widgetId = addonId(
      widgetId.substring(0, widgetId.length - "-browser-action".length)
    );
  } else if (widgetId.startsWith("pageAction-")) {
    let actionId;
    if (widgetId.startsWith("pageAction-urlbar-")) {
      actionId = widgetId.substring("pageAction-urlbar-".length);
    } else if (widgetId.startsWith("pageAction-panel-")) {
      actionId = widgetId.substring("pageAction-panel-".length);
    }

    if (actionId) {
      let action = lazy.PageActions.actionForID(actionId);
      widgetId = action?._isMozillaAction ? actionId : addonId(actionId);
    }
  } else if (widgetId.startsWith("ext-keyset-id-")) {
    // Webextension command shortcuts don't have an id on their key element so
    // we see the id from the keyset that contains them.
    widgetId = addonId(widgetId.substring("ext-keyset-id-".length));
  } else if (widgetId.startsWith("ext-key-id-")) {
    // The command for a webextension sidebar action is an exception to the above rule.
    widgetId = widgetId.substring("ext-key-id-".length);
    if (widgetId.endsWith("-sidebar-action")) {
      widgetId = addonId(
        widgetId.substring(0, widgetId.length - "-sidebar-action".length)
      );
    }
  }

  return widgetId.replace(/_/g, "-");
}

function getOpenTabsAndWinsCounts() {
  let loadedTabCount = 0;
  let tabCount = 0;
  let tabsInGroupsCount = 0;
  let winCount = 0;

  for (let win of Services.wm.getEnumerator("navigator:browser")) {
    winCount++;
    tabCount += win.gBrowser.tabs.length;
    for (const tab of win.gBrowser.tabs) {
      if (tab.getAttribute("pending") !== "true") {
        loadedTabCount += 1;
      }

      if (tab.getAttribute("group")) {
        tabsInGroupsCount += 1;
      }
    }
  }

  let tabsNotInGroupsCount = tabCount - tabsInGroupsCount;

  return {
    loadedTabCount,
    tabCount,
    winCount,
    tabsInGroupsCount,
    tabsNotInGroupsCount,
  };
}

function getPinnedTabsCount() {
  let pinnedTabs = 0;

  for (let win of Services.wm.getEnumerator("navigator:browser")) {
    pinnedTabs += [...win.ownerGlobal.gBrowser.tabs].filter(
      t => t.pinned
    ).length;
  }

  return pinnedTabs;
}

export let URICountListener = {
  // A set containing the visited domains, see bug 1271310.
  _domainSet: new Set(),
  // A set containing the visited origins during the last 24 hours (similar to domains, but not quite the same)
  _domain24hrSet: new Set(),
  // A map to keep track of the URIs loaded from the restored tabs.
  _restoredURIsMap: new WeakMap(),
  // Ongoing expiration timeouts.
  _timeouts: new Set(),

  isHttpURI(uri) {
    // Only consider http(s) schemas.
    return uri.schemeIs("http") || uri.schemeIs("https");
  },

  addRestoredURI(browser, uri) {
    if (!this.isHttpURI(uri)) {
      return;
    }

    this._restoredURIsMap.set(browser, uri.spec);
  },

  onLocationChange(browser, webProgress, request, uri, flags) {
    if (
      !(flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) &&
      webProgress.isTopLevel
    ) {
      // By default, assume we no longer need to track this tab.
      lazy.SearchSERPTelemetry.stopTrackingBrowser(
        browser,
        lazy.SearchSERPTelemetryUtils.ABANDONMENTS.NAVIGATION
      );
    }

    // Don't count this URI if it's an error page.
    if (flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE) {
      return;
    }

    // We only care about top level loads.
    if (!webProgress.isTopLevel) {
      return;
    }

    // The SessionStore sets the URI of a tab first, firing onLocationChange the
    // first time, then manages content loading using its scheduler. Once content
    // loads, we will hit onLocationChange again.
    // We can catch the first case by checking for null requests: be advised that
    // this can also happen when navigating page fragments, so account for it.
    if (
      !request &&
      !(flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT)
    ) {
      return;
    }

    // Don't include URI and domain counts when in private mode.
    let shouldCountURI =
      !lazy.PrivateBrowsingUtils.isWindowPrivate(browser.ownerGlobal) ||
      Services.prefs.getBoolPref(
        "browser.engagement.total_uri_count.pbm",
        false
      );

    // Track URI loads, even if they're not http(s).
    let uriSpec = null;
    try {
      uriSpec = uri.spec;
    } catch (e) {
      // If we have troubles parsing the spec, still count this as
      // an unfiltered URI.
      if (shouldCountURI) {
        Glean.browserEngagement.unfilteredUriCount.add(1);
      }
      return;
    }

    // Don't count about:blank and similar pages, as they would artificially
    // inflate the counts.
    if (browser.ownerGlobal.gInitialPages.includes(uriSpec)) {
      return;
    }

    // If the URI we're loading is in the _restoredURIsMap, then it comes from a
    // restored tab. If so, let's skip it and remove it from the map as we want to
    // count page refreshes.
    if (this._restoredURIsMap.get(browser) === uriSpec) {
      this._restoredURIsMap.delete(browser);
      return;
    }

    // The URI wasn't from a restored tab. Count it among the unfiltered URIs.
    // If this is an http(s) URI, this also gets counted by the "total_uri_count"
    // probe.
    if (shouldCountURI) {
      Glean.browserEngagement.unfilteredUriCount.add(1);
    }

    if (!this.isHttpURI(uri)) {
      return;
    }

    if (!(flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT)) {
      lazy.SearchSERPTelemetry.updateTrackingStatus(
        browser,
        uriSpec,
        webProgress.loadType
      );
    } else {
      lazy.SearchSERPTelemetry.updateTrackingSinglePageApp(
        browser,
        uriSpec,
        webProgress.loadType,
        flags
      );
    }

    // Update total URI count, including when in private mode.
    Glean.browserEngagement.uriCount.add(1);

    if (!shouldCountURI) {
      return;
    }

    // Update the URI counts.
    Glean.browserEngagement.uriCountNormalMode.add(1);

    // Update tab count
    BrowserUsageTelemetry._recordTabCounts(getOpenTabsAndWinsCounts());

    // Unique domains should be aggregated by (eTLD + 1): x.test.com and y.test.com
    // are counted once as test.com.
    let baseDomain;
    try {
      // Even if only considering http(s) URIs, |getBaseDomain| could still throw
      // due to the URI containing invalid characters or the domain actually being
      // an ipv4 or ipv6 address.
      baseDomain = Services.eTLD.getBaseDomain(uri);
    } catch (e) {
      return;
    }

    // We only want to count the unique domains up to MAX_UNIQUE_VISITED_DOMAINS.
    if (this._domainSet.size < MAX_UNIQUE_VISITED_DOMAINS) {
      this._domainSet.add(baseDomain);
      Glean.browserEngagement.uniqueDomainsCount.set(this._domainSet.size);
    }

    this._domain24hrSet.add(baseDomain);
    if (lazy.gRecentVisitedOriginsExpiry) {
      let timeoutId = lazy.setTimeout(() => {
        this._domain24hrSet.delete(baseDomain);
        this._timeouts.delete(timeoutId);
      }, lazy.gRecentVisitedOriginsExpiry * 1000);
      this._timeouts.add(timeoutId);
    }
  },

  /**
   * Reset the counts. This should be called when breaking a session in Telemetry.
   */
  reset() {
    this._domainSet.clear();
  },

  /**
   * Returns the number of unique domains visited in this session during the
   * last 24 hours.
   */
  get uniqueDomainsVisitedInPast24Hours() {
    return this._domain24hrSet.size;
  },

  /**
   * Resets the number of unique domains visited in this session.
   */
  resetUniqueDomainsVisitedInPast24Hours() {
    this._timeouts.forEach(timeoutId => lazy.clearTimeout(timeoutId));
    this._timeouts.clear();
    this._domain24hrSet.clear();
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
  ]),
};

let gInstallationTelemetryPromise = null;

export let BrowserUsageTelemetry = {
  /**
   * This is a policy object used to override behavior for testing.
   */
  Policy: {
    getTelemetryClientId: async () => lazy.ClientID.getClientID(),
    getUpdateDirectory: () => Services.dirsvc.get("UpdRootD", Ci.nsIFile),
    readProfileCountFile: async path => IOUtils.readUTF8(path),
    writeProfileCountFile: async (path, data) => IOUtils.writeUTF8(path, data),
  },

  _inited: false,

  /**
   * @typedef {object} TabMovementsRecord
   * @property {DeferredTask} deferredTask
   *   The `DeferredTask` that will report this record's metrics once all
   *   tab movement events with the same `telemetrySource` have been received
   *   in the current event loop.
   * @property {number} numberAddedToTabGroup
   *   The number of tabs from `tabs` which started out as ungrouped tabs but
   *   moved into a tab group during the tab movement operation.
   */

  /** @type {Map<string, TabMovementsRecord>} */
  _tabMovementsBySource: new Map(),

  init() {
    this._lastRecordTabCount = 0;
    this._lastRecordLoadedTabCount = 0;
    this._setupAfterRestore();
    this._inited = true;

    Services.prefs.addObserver("browser.tabs.inTitlebar", this);
    Services.prefs.addObserver(
      "media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled",
      this
    );

    this._recordUITelemetry();

    this._onTabsOpenedTask = new lazy.DeferredTask(
      () => this._onTabsOpened(),
      0
    );

    this._onTabGroupChangeTask = new lazy.DeferredTask(
      () => this._doOnTabGroupChange(),
      0
    );

    this._onTabGroupExpandOrCollapseTask = new lazy.DeferredTask(
      () => this._doOnTabGroupExpandOrCollapse(),
      0
    );

    this._onSavedTabGroupsChangedTask = new lazy.DeferredTask(
      () => this._doOnSavedTabGroupsChange(),
      0
    );
    this._onSavedTabGroupsChangedTask.arm();
  },

  maxWindowCount: 0,
  maxTabCount: 0,
  get maxTabCountGleanQuantity() {
    return lazy.sidebarVerticalTabs
      ? Glean.browserEngagement.maxConcurrentVerticalTabCount
      : Glean.browserEngagement.maxConcurrentTabCount;
  },

  maxTabPinnedCount: 0,
  updateMaxTabPinnedCount(pinnedTabs) {
    if (pinnedTabs > this.maxTabPinnedCount) {
      this.maxTabPinnedCount = pinnedTabs;
      if (lazy.sidebarVerticalTabs) {
        Glean.browserEngagement.maxConcurrentVerticalTabPinnedCount.set(
          pinnedTabs
        );
      } else {
        Glean.browserEngagement.maxConcurrentTabPinnedCount.set(pinnedTabs);
      }
    }
  },

  /**
   * Resets the masked add-on identifiers. Only for use in tests.
   */
  _resetAddonIds() {
    KNOWN_ADDONS.length = 0;
  },

  /**
   * Handle subsession splits in the parent process.
   */
  afterSubsessionSplit() {
    // Scalars just got cleared due to a subsession split. We need to set the maximum
    // concurrent tab and window counts so that they reflect the correct value for the
    // new subsession.
    this._initMaxTabAndWindowCounts();

    // Reset the URI counter.
    URICountListener.reset();
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  uninit() {
    if (!this._inited) {
      return;
    }
    Services.obs.removeObserver(this, DOMWINDOW_OPENED_TOPIC);
    Services.obs.removeObserver(this, TELEMETRY_SUBSESSIONSPLIT_TOPIC);
    Services.obs.removeObserver(this, SESSION_STORE_SAVED_TAB_GROUPS_TOPIC);
  },

  observe(subject, topic, data) {
    switch (topic) {
      case DOMWINDOW_OPENED_TOPIC:
        this._onWindowOpen(subject);
        break;
      case TELEMETRY_SUBSESSIONSPLIT_TOPIC:
        this.afterSubsessionSplit();
        break;
      case SESSION_STORE_SAVED_TAB_GROUPS_TOPIC:
        this._onSavedTabGroupsChange();
        break;
      case "nsPref:changed":
        switch (data) {
          case "browser.tabs.inTitlebar":
            this._recordWidgetChange(
              "titlebar",
              Services.appinfo.drawInTitlebar ? "off" : "on",
              "pref"
            );
            break;
          case "media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled":
            if (Services.prefs.getBoolPref(data)) {
              Glean.pictureinpictureSettings.enableAutotriggerSettings.record();
            }
            break;
        }
        break;
    }
  },

  handleEvent(event) {
    switch (event.type) {
      case "TabOpen":
        this._onTabOpen(event);
        break;
      case "TabClose":
        this._onTabClosed(event);
        break;
      case "TabPinned":
        this._onTabPinned();
        break;
      case "TabGroupCreate":
        this._onTabGroupCreate(event);
        break;
      case "TabGrouped":
      case "TabUngrouped":
        this._onTabGroupChange();
        break;
      case "TabGroupCollapse":
      case "TabGroupExpand":
        this._onTabGroupExpandOrCollapse();
        break;
      case "TabMove":
        this._onTabMove(event);
        break;
      case "TabSelect":
        this._onTabSelect(event);
        break;
      case "TabGroupRemoveRequested":
        this._onTabGroupRemoveRequested(event);
        break;
      case "TabGroupSaved":
        this._onTabGroupSave(event);
        break;
      case "TabGroupUngroup":
        this._onTabGroupUngroup(event);
        break;

      case "unload":
        this._unregisterWindow(event.target);
        break;
      case TAB_RESTORING_TOPIC:
        // We're restoring a new tab from a previous or crashed session.
        // We don't want to track the URIs from these tabs, so let
        // |URICountListener| know about them.
        let browser = event.target.linkedBrowser;
        URICountListener.addRestoredURI(browser, browser.currentURI);

        const { loadedTabCount } = getOpenTabsAndWinsCounts();
        this._recordTabCounts({ loadedTabCount });
        break;
    }
  },

  _initMaxTabAndWindowCounts() {
    const counts = getOpenTabsAndWinsCounts();
    this.maxTabCount = counts.tabCount;
    this.maxTabCountGleanQuantity.set(counts.tabCount);
    this.maxWindowCount = counts.winCount;
    Glean.browserEngagement.maxConcurrentWindowCount.set(counts.winCount);
  },

  /**
   * This gets called shortly after the SessionStore has finished restoring
   * windows and tabs. It counts the open tabs and adds listeners to all the
   * windows.
   */
  _setupAfterRestore() {
    // Make sure to catch new chrome windows and subsession splits.
    Services.obs.addObserver(this, DOMWINDOW_OPENED_TOPIC, true);
    Services.obs.addObserver(this, TELEMETRY_SUBSESSIONSPLIT_TOPIC, true);
    Services.obs.addObserver(this, SESSION_STORE_SAVED_TAB_GROUPS_TOPIC, true);

    // Attach the tabopen handlers to the existing Windows.
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      this._registerWindow(win);
    }

    // Get the initial tab and windows max counts.
    this._initMaxTabAndWindowCounts();
  },

  _buildWidgetPositions() {
    let widgetMap = new Map();

    const toolbarState = nodeId => {
      let value;
      if (nodeId == "PersonalToolbar") {
        value = Services.prefs.getCharPref(
          "browser.toolbars.bookmarks.visibility",
          "newtab"
        );
        if (value != "newtab") {
          return value == "never" ? "off" : "on";
        }
        return value;
      }
      value = Services.xulStore.getValue(
        AppConstants.BROWSER_CHROME_URL,
        nodeId,
        "collapsed"
      );

      if (value) {
        return value == "true" ? "off" : "on";
      }
      return "off";
    };

    widgetMap.set(
      BROWSER_UI_CONTAINER_IDS.PersonalToolbar,
      toolbarState("PersonalToolbar")
    );

    let menuBarHidden =
      Services.xulStore.getValue(
        AppConstants.BROWSER_CHROME_URL,
        "toolbar-menubar",
        "autohide"
      ) != "false";

    widgetMap.set("menu-toolbar", menuBarHidden ? "off" : "on");

    // Drawing in the titlebar means not showing the titlebar, hence the negation.
    widgetMap.set("titlebar", Services.appinfo.drawInTitlebar ? "off" : "on");

    for (let area of lazy.CustomizableUI.areas) {
      if (!(area in BROWSER_UI_CONTAINER_IDS)) {
        continue;
      }

      let position = BROWSER_UI_CONTAINER_IDS[area];
      if (area == "nav-bar") {
        position = `${BROWSER_UI_CONTAINER_IDS[area]}-start`;
      }

      let widgets = lazy.CustomizableUI.getWidgetsInArea(area);

      for (let widget of widgets) {
        if (!widget) {
          continue;
        }

        if (widget.id.startsWith("customizableui-special-")) {
          continue;
        }

        if (area == "nav-bar" && widget.id == "urlbar-container") {
          position = `${BROWSER_UI_CONTAINER_IDS[area]}-end`;
          continue;
        }

        widgetMap.set(widget.id, position);
      }
    }

    let actions = lazy.PageActions.actions;
    for (let action of actions) {
      if (action.pinnedToUrlbar) {
        widgetMap.set(action.id, "pageaction-urlbar");
      }
    }

    return widgetMap;
  },

  _getWidgetID(node) {
    // We want to find a sensible ID for this element.
    if (!node) {
      return null;
    }

    // See if this is a customizable widget.
    if (node.ownerDocument.URL == AppConstants.BROWSER_CHROME_URL) {
      // First find if it is inside one of the customizable areas.
      for (let area of lazy.CustomizableUI.areas) {
        if (node.closest(`#${CSS.escape(area)}`)) {
          for (let widget of lazy.CustomizableUI.getWidgetIdsInArea(area)) {
            if (
              // We care about the buttons on the tabs themselves.
              widget == "tabbrowser-tabs" ||
              // We care about the page action and other buttons in here.
              widget == "urlbar-container" ||
              // We care about the actual menu items.
              widget == "menubar-items" ||
              // We care about individual bookmarks here.
              widget == "personal-bookmarks"
            ) {
              continue;
            }

            if (node.closest(`#${CSS.escape(widget)}`)) {
              return widget;
            }
          }
          break;
        }
      }
    }

    if (node.id) {
      return node.id;
    }

    // A couple of special cases in the tabs.
    for (let cls of ["bookmark-item", "tab-icon-sound", "tab-close-button"]) {
      if (!node.classList.contains(cls)) {
        continue;
      }
      if (cls == "bookmark-item" && node.parentElement.id.includes("history")) {
        return "history-item";
      }
      return cls;
    }

    // One of these will at least let us know what the widget is for.
    let possibleAttributes = [
      "preference",
      "command",
      "observes",
      "data-l10n-id",
    ];

    // The key attribute on key elements is the actual key to listen for.
    if (node.localName != "key") {
      possibleAttributes.unshift("key");
    }

    for (let idAttribute of possibleAttributes) {
      if (node.hasAttribute(idAttribute)) {
        return node.getAttribute(idAttribute);
      }
    }

    return this._getWidgetID(node.parentElement);
  },

  _getBrowserWidgetContainer(node) {
    // Find the container holding this element.
    for (let containerId of Object.keys(BROWSER_UI_CONTAINER_IDS)) {
      let container = node.ownerDocument.getElementById(containerId);
      if (container && container.contains(node)) {
        return BROWSER_UI_CONTAINER_IDS[containerId];
      }
    }
    // Treat toolbar context menu items that relate to tabs as the tab menu:
    if (
      node.closest("#toolbar-context-menu") &&
      node.getAttribute("contexttype") == "tabbar"
    ) {
      return BROWSER_UI_CONTAINER_IDS.tabContextMenu;
    }
    return null;
  },

  _getWidgetContainer(node) {
    if (node.localName == "key") {
      return "keyboard";
    }

    const { URL: url } = node.ownerDocument;
    if (url == AppConstants.BROWSER_CHROME_URL) {
      return this._getBrowserWidgetContainer(node);
    }
    if (
      url.startsWith("about:preferences") ||
      url.startsWith("about:settings")
    ) {
      // Find the element's category.
      let container = node.closest("[data-category]");
      if (!container) {
        return null;
      }

      let pane = container.getAttribute("data-category");

      if (!PREFERENCES_PANES.includes(pane)) {
        pane = "paneUnknown";
      }

      return `preferences_${pane}`;
    }

    return null;
  },

  lastClickTarget: null,

  ignoreEvent(event) {
    IGNORABLE_EVENTS.set(event, true);
  },

  _recordCommand(event) {
    if (IGNORABLE_EVENTS.get(event)) {
      return;
    }

    let sourceEvent = event;
    while (sourceEvent.sourceEvent) {
      sourceEvent = sourceEvent.sourceEvent;
    }

    let lastTarget = this.lastClickTarget?.get();
    if (
      lastTarget &&
      sourceEvent.type == "command" &&
      sourceEvent.target.contains(lastTarget)
    ) {
      // Ignore a command event triggered by a click.
      this.lastClickTarget = null;
      return;
    }

    this.lastClickTarget = null;

    if (sourceEvent.type == "click") {
      // Only care about main button clicks.
      if (sourceEvent.button != 0) {
        return;
      }

      // This click may trigger a command event so retain the target to be able
      // to dedupe that event.
      this.lastClickTarget = Cu.getWeakReference(sourceEvent.target);
    }

    // We should never see events from web content as they are fired in a
    // content process, but let's be safe.
    let url = sourceEvent.target.ownerDocument.documentURIObject;
    if (!url.schemeIs("chrome") && !url.schemeIs("about")) {
      return;
    }

    // This is what events targetted  at content will actually look like.
    if (sourceEvent.target.localName == "browser") {
      return;
    }

    // Find the actual element we're interested in.
    let node = sourceEvent.target;
    const isAboutPreferences =
      node.ownerDocument.URL.startsWith("about:preferences") ||
      node.ownerDocument.URL.startsWith("about:settings");
    while (
      !UI_TARGET_ELEMENTS.includes(node.localName) &&
      !node.classList?.contains("wants-telemetry") &&
      // We are interested in links on about:preferences as well.
      !(
        isAboutPreferences &&
        (node.getAttribute("is") === "text-link" || node.localName === "a")
      )
    ) {
      node = node.parentNode;
      if (!node?.parentNode) {
        // A click on a space or label or top-level document or something we're
        // not interested in.
        return;
      }
    }

    // When the expected target is a Custom Element with a Shadow Root, there
    // may be a specific part of the component that click events correspond to
    // changes. Ignore any other events if requested.
    let expectedEventTarget = UI_TARGET_COMPOSED_ELEMENTS_MAP.get(
      node.localName
    );
    if (
      event.type == "click" &&
      expectedEventTarget &&
      expectedEventTarget != event.composedTarget?.localName
    ) {
      return;
    }

    if (sourceEvent.type === "command") {
      const { command, ownerDocument, parentNode } = node;
      // Check if this command is for a history or bookmark link being opened
      // from the context menu. In this case, we are interested in the DOM node
      // for the link, not the menu item itself.
      if (
        PLACES_OPEN_COMMANDS.includes(command) ||
        parentNode?.parentNode?.id === PLACES_OPEN_IN_CONTAINER_TAB_MENU_ID
      ) {
        node = ownerDocument.getElementById(PLACES_CONTEXT_MENU_ID).triggerNode;
      }
    }

    let item = this._getWidgetID(node);
    let source = this._getWidgetContainer(node);

    if (item && source) {
      this.recordInteractionEvent(item, source);
      let name = source
        .replace(/-/g, "_")
        .replace(/_([a-z])/g, (m, p) => p.toUpperCase());
      Glean.browserUiInteraction[name]?.[telemetryId(item)].add(1);
      if (SET_USAGECOUNT_PREF_BUTTONS.includes(item)) {
        let pref = `browser.engagement.${item}.used-count`;
        Services.prefs.setIntPref(pref, Services.prefs.getIntPref(pref, 0) + 1);
      }
      if (SET_USAGE_PREF_BUTTONS.includes(item)) {
        Services.prefs.setBoolPref(`browser.engagement.${item}.has-used`, true);
      }
    }

    if (ENTRYPOINT_TRACKED_CONTEXT_MENU_IDS[source]) {
      let contextMenu = ENTRYPOINT_TRACKED_CONTEXT_MENU_IDS[source];
      let triggerContainer = this._getWidgetContainer(
        node.closest("menupopup")?.triggerNode
      );
      if (triggerContainer) {
        this.recordInteractionEvent(item, contextMenu);
        let name = contextMenu
          .replace(/-/g, "_")
          .replace(/_([a-z])/g, (m, p) => p.toUpperCase());
        Glean.browserUiInteraction[name]?.[telemetryId(triggerContainer)].add(
          1
        );
      }
    }
  },

  _flowId: null,
  _flowIdTS: 0,

  recordInteractionEvent(widgetId, source) {
    // A note on clocks. Cu.now() is monotonic, but its behaviour across
    // computer sleeps is different per platform.
    // We're okay with this for flows because we're looking at idle times
    // on the order of minutes and within the same machine, so the weirdest
    // thing we may expect is a flow that accidentally continues across a
    // sleep. Until we have evidence that this is common, we're in the clear.
    if (!this._flowId || this._flowIdTS + FLOW_IDLE_TIME < Cu.now()) {
      // We submit the ping full o' events on every new flow,
      // including at startup.
      GleanPings.prototypeNoCodeEvents.submit();
      // We use a GUID here because we need to identify events in a flow
      // out of all events from all flows across all clients.
      this._flowId = Services.uuid.generateUUID();
    }
    this._flowIdTS = Cu.now();

    const extra = {
      source,
      widget_id: telemetryId(widgetId),
      flow_id: this._flowId,
    };
    Glean.browserUsage.interaction.record(extra);
  },

  /**
   * Listens for UI interactions in the window.
   */
  _addUsageListeners(win) {
    // Listen for command events from the UI.
    win.addEventListener("command", event => this._recordCommand(event), true);
    win.addEventListener("click", event => this._recordCommand(event), true);
  },

  /**
   * A public version of the private method to take care of the `nav-bar-start`,
   * `nav-bar-end` thing that callers shouldn't have to care about. It also
   * accepts the DOM ids for the areas rather than the cleaner ones we report
   * to telemetry.
   */
  recordWidgetChange(widgetId, newPos, reason) {
    try {
      if (newPos) {
        newPos = BROWSER_UI_CONTAINER_IDS[newPos];
      }

      if (newPos == "nav-bar") {
        let { position } = lazy.CustomizableUI.getPlacementOfWidget(widgetId);
        let { position: urlPosition } =
          lazy.CustomizableUI.getPlacementOfWidget("urlbar-container");
        newPos = newPos + (urlPosition > position ? "-start" : "-end");
      }

      this._recordWidgetChange(widgetId, newPos, reason);
    } catch (e) {
      console.error(e);
    }
  },

  recordToolbarVisibility(toolbarId, newState, reason) {
    if (typeof newState != "string") {
      newState = newState ? "on" : "off";
    }
    this._recordWidgetChange(
      BROWSER_UI_CONTAINER_IDS[toolbarId],
      newState,
      reason
    );
  },

  _recordWidgetChange(widgetId, newPos, reason) {
    // In some cases (like when add-ons are detected during startup) this gets
    // called before we've reported the initial positions. Ignore such cases.
    if (!this.widgetMap) {
      return;
    }

    if (widgetId == "urlbar-container") {
      // We don't report the position of the url bar, it is after nav-bar-start
      // and before nav-bar-end. But moving it means the widgets around it have
      // effectively moved so update those.
      let position = "nav-bar-start";
      let widgets = lazy.CustomizableUI.getWidgetsInArea("nav-bar");

      for (let widget of widgets) {
        if (!widget) {
          continue;
        }

        if (widget.id.startsWith("customizableui-special-")) {
          continue;
        }

        if (widget.id == "urlbar-container") {
          position = "nav-bar-end";
          continue;
        }

        // This will do nothing if the position hasn't changed.
        this._recordWidgetChange(widget.id, position, reason);
      }

      return;
    }

    let oldPos = this.widgetMap.get(widgetId);
    if (oldPos == newPos) {
      return;
    }

    let action = "move";

    if (!oldPos) {
      action = "add";
    } else if (!newPos) {
      action = "remove";
    }

    let key = `${telemetryId(widgetId, false)}_${action}_${oldPos ?? "na"}_${
      newPos ?? "na"
    }_${reason}`;
    Glean.browserUi.customizedWidgets[key].add(1);

    if (newPos) {
      this.widgetMap.set(widgetId, newPos);
    } else {
      this.widgetMap.delete(widgetId);
    }
  },

  _recordUITelemetry() {
    this.widgetMap = this._buildWidgetPositions();

    // FIXME(bug 1883857): object metric type not available in artefact builds.
    if ("toolbarWidgets" in Glean.browserUi) {
      Glean.browserUi.toolbarWidgets.set(
        this.widgetMap
          .entries()
          .map(([widgetId, position]) => {
            return { widgetId: telemetryId(widgetId, false), position };
          })
          .toArray()
      );
    }

    for (let [widgetId, position] of this.widgetMap.entries()) {
      let key = `${telemetryId(widgetId, false)}_pinned_${position}`;
      Glean.browserUi.mirrorForToolbarWidgets[key].set(true);
    }
  },

  /**
   * Adds listeners to a single chrome window.
   * @param {Window} win
   */
  _registerWindow(win) {
    this._addUsageListeners(win);

    win.addEventListener("unload", this);
    win.addEventListener("TabMove", this);
    win.addEventListener("TabOpen", this, true);
    win.addEventListener("TabClose", this, true);
    win.addEventListener("TabPinned", this, true);
    win.addEventListener("TabSelect", this);
    win.addEventListener("TabGroupCreate", this);
    win.addEventListener("TabGroupRemoveRequested", this);
    win.addEventListener("TabGrouped", this);
    win.addEventListener("TabUngrouped", this);
    win.addEventListener("TabGroupCollapse", this);
    win.addEventListener("TabGroupExpand", this);
    win.addEventListener("TabGroupSaved", this);
    win.addEventListener("TabGroupUngroup", this);

    win.gBrowser.tabContainer.addEventListener(TAB_RESTORING_TOPIC, this);
    win.gBrowser.addTabsProgressListener(URICountListener);
  },

  /**
   * Removes listeners from a single chrome window.
   */
  _unregisterWindow(win) {
    win.removeEventListener("unload", this);
    win.removeEventListener("TabMove", this);
    win.removeEventListener("TabOpen", this, true);
    win.removeEventListener("TabClose", this, true);
    win.removeEventListener("TabPinned", this, true);
    win.removeEventListener("TabSelect", this);
    win.removeEventListener("TabGroupCreate", this);
    win.removeEventListener("TabGroupRemoveRequested", this);
    win.removeEventListener("TabGrouped", this);
    win.removeEventListener("TabUngrouped", this);
    win.removeEventListener("TabGroupCollapse", this);
    win.removeEventListener("TabGroupExpand", this);
    win.removeEventListener("TabGroupSaved", this);
    win.removeEventListener("TabGroupUngroup", this);

    win.defaultView.gBrowser.tabContainer.removeEventListener(
      TAB_RESTORING_TOPIC,
      this
    );
    win.defaultView.gBrowser.removeTabsProgressListener(URICountListener);
  },

  /**
   * Updates the tab counts.
   */
  _onTabOpen(event) {
    // Update the "tab opened" count and its maximum.
    if (lazy.sidebarVerticalTabs) {
      Glean.browserEngagement.verticalTabOpenEventCount.add(1);
    } else {
      Glean.browserEngagement.tabOpenEventCount.add(1);
    }

    if (event?.target?.group) {
      Glean.tabgroup.tabInteractions.new.add();
    }

    // In the case of opening multiple tabs at once, avoid enumerating all open
    // tabs and windows each time a tab opens.
    this._onTabsOpenedTask.disarm();
    this._onTabsOpenedTask.arm();
  },

  /**
   * Update tab counts after opening multiple tabs.
   */
  _onTabsOpened() {
    const { tabCount, loadedTabCount } = getOpenTabsAndWinsCounts();
    if (tabCount > this.maxTabCount) {
      this.maxTabCount = tabCount;
      this.maxTabCountGleanQuantity.set(tabCount);
    }

    this._recordTabCounts({ tabCount, loadedTabCount });
  },

  _onTabClosed(event) {
    const group = event.target?.group;
    const source = event.detail?.telemetrySource;

    if (group) {
      if (source == lazy.TabMetrics.METRIC_SOURCE.TAB_STRIP) {
        Glean.tabgroup.tabInteractions.close_tabstrip.add();
      } else if (source == lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU) {
        Glean.tabgroup.tabInteractions.close_tabmenu.add();
      }
    }
  },

  _onTabPinned() {
    const pinnedTabs = getPinnedTabsCount();

    // Update the "tab pinned" count and its maximum.
    if (lazy.sidebarVerticalTabs) {
      Glean.browserEngagement.verticalTabPinnedEventCount.add(1);
    } else {
      Glean.browserEngagement.tabPinnedEventCount.add(1);
    }
    this.updateMaxTabPinnedCount(pinnedTabs);
  },

  _onTabGroupCreate(event) {
    if (event.detail.isUserTriggered) {
      Glean.tabgroup.createGroup.record({
        id: event.target.id,
        layout: lazy.sidebarVerticalTabs
          ? lazy.TabMetrics.METRIC_TABS_LAYOUT.VERTICAL
          : lazy.TabMetrics.METRIC_TABS_LAYOUT.HORIZONTAL,
        source: event.detail.telemetryUserCreateSource,
        tabs: event.target.tabs.length,
      });
    }

    this._onTabGroupChange();
  },

  _onTabGroupSave(event) {
    const { isUserTriggered } = event.detail;

    Glean.tabgroup.save.record({
      user_triggered: isUserTriggered,
      id: event.target.id,
    });

    if (isUserTriggered) {
      Glean.tabgroup.groupInteractions.save.add(1);
    }

    this._onTabGroupChange();
  },

  _onTabGroupChange() {
    this._onTabGroupChangeTask.disarm();
    this._onTabGroupChangeTask.arm();
  },

  /**
   * @param {CustomEvent} event `TabGroupUngroup` event
   */
  _onTabGroupUngroup(event) {
    const { isUserTriggered } = event.detail;
    if (isUserTriggered) {
      Glean.tabgroup.groupInteractions.ungroup.add(1);
    }
  },

  /**
   * Returns summary statistics of a set of numbers.
   *
   * @param {number[]} data
   * @returns {{max: number, min: number, median: number, average: number}}
   */
  _getSummaryStats(data) {
    let count = data.length;
    data.sort((a, b) => a - b);
    let middleIndex = Math.floor(count / 2);

    return {
      max: data.at(-1),
      min: data.at(0),
      median:
        count % 2 == 0
          ? (data[middleIndex - 1] + data[middleIndex]) / 2
          : data[middleIndex],
      average: data.reduce((a, b) => a + b, 0) / count,
    };
  },

  _doOnTabGroupChange() {
    let totalTabs = 0;
    let totalTabsInGroups = 0;

    // Used for calculation of average and median
    let tabGroupLengths = [];

    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      totalTabs += win.gBrowser.tabs.length;
      for (let group of win.gBrowser.tabGroups) {
        totalTabsInGroups += group.tabs.length;
        tabGroupLengths.push(group.tabs.length);
      }
    }

    let { max, min, median, average } = this._getSummaryStats(tabGroupLengths);

    Glean.tabgroup.tabCountInGroups.inside.set(totalTabsInGroups);
    Glean.tabgroup.tabCountInGroups.outside.set(totalTabs - totalTabsInGroups);

    Glean.tabgroup.tabsPerActiveGroup.median.set(median);
    Glean.tabgroup.tabsPerActiveGroup.average.set(average);
    Glean.tabgroup.tabsPerActiveGroup.max.set(max);
    Glean.tabgroup.tabsPerActiveGroup.min.set(min);
  },

  _onSavedTabGroupsChange() {
    this._onSavedTabGroupsChangedTask.disarm();
    this._onSavedTabGroupsChangedTask.arm();
  },

  _doOnSavedTabGroupsChange() {
    let savedGroups = lazy.SessionStore.getSavedTabGroups();
    let tabCounts = savedGroups.map(group => group.tabs.length);
    let { max, min, median, average } = this._getSummaryStats(tabCounts);

    Glean.tabgroup.savedGroups.set(savedGroups.length);

    Glean.tabgroup.tabsPerSavedGroup.median.set(median);
    Glean.tabgroup.tabsPerSavedGroup.average.set(average);
    Glean.tabgroup.tabsPerSavedGroup.max.set(max);
    Glean.tabgroup.tabsPerSavedGroup.min.set(min);
  },

  _onTabGroupExpandOrCollapse() {
    this._onTabGroupExpandOrCollapseTask.disarm();
    this._onTabGroupExpandOrCollapseTask.arm();
  },

  _doOnTabGroupExpandOrCollapse() {
    let expanded = 0,
      collapsed = 0;

    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      for (let group of win.gBrowser.tabGroups) {
        if (group.collapsed) {
          collapsed += 1;
        } else {
          expanded += 1;
        }
      }
    }

    Glean.tabgroup.activeGroups.collapsed.set(collapsed);
    Glean.tabgroup.activeGroups.expanded.set(expanded);
  },

  /**
   * @param {CustomEvent} event
   */
  _onTabGroupRemoveRequested(event) {
    let {
      isUserTriggered = false,
      telemetrySource = lazy.TabMetrics.METRIC_SOURCE.UNKNOWN,
    } = event.detail;

    if (isUserTriggered) {
      Glean.tabgroup.delete.record({
        id: event.target.id,
        source: telemetrySource,
      });
      Glean.tabgroup.groupInteractions.delete.add(1);
    }
  },

  /**
   * Accumulates `TabMove` events in order to record 1 metrics event per frame
   * per telemetry source.
   *
   * For example, dragging and dropping 4 tabs should listen for 4 `TabMove`
   * events but result in 1 metrics event being recorded with a source of
   * `drag` and a tab count of 4.
   *
   * @param {CustomEvent} event
   */
  _onTabMove(event) {
    let { isUserTriggered, telemetrySource } = event.detail;

    if (!isUserTriggered) {
      return;
    }

    let tabMovementsRecord = this._tabMovementsBySource.get(telemetrySource);
    if (!tabMovementsRecord) {
      let deferredTask = new lazy.DeferredTask(() => {
        Glean.tabgroup.addTab.record({
          source: telemetrySource,
          tabs: tabMovementsRecord.numberAddedToTabGroup,
          layout: lazy.sidebarVerticalTabs ? "vertical" : "horizontal",
        });
        this._tabMovementsBySource.delete(telemetrySource);
      }, 0);
      tabMovementsRecord = {
        deferredTask,
        numberAddedToTabGroup: 0,
      };
      this._tabMovementsBySource.set(telemetrySource, tabMovementsRecord);
      this._updateTabMovementsRecord(tabMovementsRecord, event);
      deferredTask.arm();
    } else {
      tabMovementsRecord.deferredTask.disarm();
      this._updateTabMovementsRecord(tabMovementsRecord, event);
      tabMovementsRecord.deferredTask.arm();
    }
  },

  /**
   * @param {TabMovementsRecord} record
   * @param {CustomEvent} event
   */
  _updateTabMovementsRecord(record, event) {
    let { previousTabState, currentTabState } = event.detail;

    if (!previousTabState.tabGroupId && currentTabState.tabGroupId) {
      Glean.tabgroup.tabInteractions.add.add();
      record.numberAddedToTabGroup += 1;
    }

    if (
      previousTabState.tabGroupId &&
      previousTabState.tabGroupId == currentTabState.tabGroupId &&
      previousTabState.tabIndex != currentTabState.tabIndex
    ) {
      Glean.tabgroup.tabInteractions.reorder.add();
    }

    if (previousTabState.tabGroupId && !currentTabState.tabGroupId) {
      Glean.tabgroup.tabInteractions.remove_same_window.add();
    }
  },

  _onTabSelect(event) {
    if (event.target.group) {
      Glean.tabgroup.tabInteractions.activate.add();
    }
  },

  /**
   * Tracks the window count and registers the listeners for the tab count.
   * @param{Object} win The window object.
   */
  _onWindowOpen(win) {
    // Make sure to have a |nsIDOMWindow|.
    if (!(win instanceof Ci.nsIDOMWindow)) {
      return;
    }

    let onLoad = () => {
      win.removeEventListener("load", onLoad);

      // Ignore non browser windows.
      if (
        win.document.documentElement.getAttribute("windowtype") !=
        "navigator:browser"
      ) {
        return;
      }

      this._registerWindow(win);
      // Track the window open event and check the maximum.
      const counts = getOpenTabsAndWinsCounts();
      Glean.browserEngagement.windowOpenEventCount.add(1);

      if (counts.winCount > this.maxWindowCount) {
        this.maxWindowCount = counts.winCount;
        Glean.browserEngagement.maxConcurrentWindowCount.set(counts.winCount);
      }

      // We won't receive the "TabOpen" event for the first tab within a new window.
      // Account for that.
      this._onTabOpen();
    };
    win.addEventListener("load", onLoad);
  },

  /**
   * Record telemetry about the given tab counts.
   *
   * Telemetry for each count will only be recorded if the value isn't
   * `undefined`.
   *
   * @param {object} [counts] The tab counts to register with telemetry.
   * @param {number} [counts.tabCount] The number of tabs in all browsers.
   * @param {number} [counts.loadedTabCount] The number of loaded (i.e., not
   *                                         pending) tabs in all browsers.
   */
  _recordTabCounts({ tabCount, loadedTabCount }) {
    let currentTime = Date.now();
    if (
      tabCount !== undefined &&
      currentTime > this._lastRecordTabCount + MINIMUM_TAB_COUNT_INTERVAL_MS
    ) {
      Glean.browserEngagement.tabCount.accumulateSingleSample(tabCount);
      this._lastRecordTabCount = currentTime;
    }

    if (
      loadedTabCount !== undefined &&
      currentTime >
        this._lastRecordLoadedTabCount + MINIMUM_TAB_COUNT_INTERVAL_MS
    ) {
      Glean.browserEngagement.loadedTabCount.accumulateSingleSample(
        loadedTabCount
      );
      this._lastRecordLoadedTabCount = currentTime;
    }
  },

  _checkProfileCountFileSchema(fileData) {
    // Verifies that the schema of the file is the expected schema
    if (typeof fileData.version != "string") {
      throw new Error("Schema Mismatch Error: Bad type for 'version' field");
    }
    if (!Array.isArray(fileData.profileTelemetryIds)) {
      throw new Error(
        "Schema Mismatch Error: Bad type for 'profileTelemetryIds' field"
      );
    }
    for (let profileTelemetryId of fileData.profileTelemetryIds) {
      if (typeof profileTelemetryId != "string") {
        throw new Error(
          "Schema Mismatch Error: Bad type for an element of 'profileTelemetryIds'"
        );
      }
    }
  },

  // Reports the number of Firefox profiles on this machine to telemetry.
  async reportProfileCount() {
    // Note: this is currently a windows-only feature.

    // To report only as much data as we need, we will bucket our values.
    // Rather than the raw value, we will report the greatest value in the list
    // below that is no larger than the raw value.
    const buckets = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100, 1000, 10000];

    // We need both the C:\ProgramData\Mozilla directory and the install
    // directory hash to create the profile count file path. We can easily
    // reassemble this from the update directory, which looks like:
    // C:\ProgramData\Mozilla\updates\hash
    // Retrieving the directory this way also ensures that the "Mozilla"
    // directory is created with the correct permissions.
    // The ProgramData directory, by default, grants write permissions only to
    // file creators. The directory service calls GetCommonUpdateDirectory,
    // which makes sure the the directory is created with user-writable
    // permissions.
    const updateDirectory = BrowserUsageTelemetry.Policy.getUpdateDirectory();
    const hash = updateDirectory.leafName;
    const profileCountFilename = "profile_count_" + hash + ".json";
    let profileCountFile = updateDirectory.parent.parent;
    profileCountFile.append(profileCountFilename);

    let readError = false;
    let fileData;
    try {
      let json = await BrowserUsageTelemetry.Policy.readProfileCountFile(
        profileCountFile.path
      );
      fileData = JSON.parse(json);
      BrowserUsageTelemetry._checkProfileCountFileSchema(fileData);
    } catch (ex) {
      // Note that since this also catches the "no such file" error, this is
      // always the template that we use when writing to the file for the first
      // time.
      fileData = { version: "1", profileTelemetryIds: [] };
      if (!(ex.name == "NotFoundError")) {
        console.error(ex);
        // Don't just return here on a read error. We need to send the error
        // value to telemetry and we want to attempt to fix the file.
        // However, we will still report an error for this ping, even if we
        // fix the file. This is to prevent always sending a profile count of 1
        // if, for some reason, we always get a read error but never a write
        // error.
        readError = true;
      }
    }

    let writeError = false;
    let currentTelemetryId =
      await BrowserUsageTelemetry.Policy.getTelemetryClientId();
    // Don't add our telemetry ID to the file if we've already reached the
    // largest bucket. This prevents the file size from growing forever.
    if (
      !fileData.profileTelemetryIds.includes(currentTelemetryId) &&
      fileData.profileTelemetryIds.length < Math.max(...buckets)
    ) {
      fileData.profileTelemetryIds.push(currentTelemetryId);
      try {
        await BrowserUsageTelemetry.Policy.writeProfileCountFile(
          profileCountFile.path,
          JSON.stringify(fileData)
        );
      } catch (ex) {
        console.error(ex);
        writeError = true;
      }
    }

    // Determine the bucketed value to report
    let rawProfileCount = fileData.profileTelemetryIds.length;
    let valueToReport = 0;
    for (let bucket of buckets) {
      if (bucket <= rawProfileCount && bucket > valueToReport) {
        valueToReport = bucket;
      }
    }

    if (readError || writeError) {
      // We convey errors via a profile count of 0.
      valueToReport = 0;
    }

    Glean.browserEngagement.profileCount.set(valueToReport);
  },

  /**
   * Check if this is the first run of this profile since installation,
   * if so then collect installation telemetry.
   *
   * @param {nsIFile} [dataPathOverride] Optional, full data file path, for tests.
   * @param {Array<string>} [msixPackagePrefixes] Optional, list of prefixes to
            consider "existing" installs when looking at installed MSIX packages.
            Defaults to prefixes for builds produced in Firefox automation.
   * @return {Promise<Object>} A JSON object containing install telemetry.
   * @resolves When the event has been recorded, or if the data file was not found.
   * @rejects JavaScript exception on any failure.
   */
  async collectInstallationTelemetry(
    dataPathOverride,
    msixPackagePrefixes = ["Mozilla.Firefox", "Mozilla.MozillaFirefox"]
  ) {
    if (AppConstants.platform != "win") {
      // This is a windows-only feature.
      return {};
    }

    const TIMESTAMP_PREF = "app.installation.timestamp";
    const lastInstallTime = Services.prefs.getStringPref(TIMESTAMP_PREF, null);
    const wpm = Cc["@mozilla.org/windows-package-manager;1"].createInstance(
      Ci.nsIWindowsPackageManager
    );
    let installer_type = "";
    let pfn;
    try {
      pfn = Services.sysinfo.getProperty("winPackageFamilyName");
    } catch (e) {}

    function getInstallData() {
      // We only care about where _any_ other install existed - no
      // need to count more than 1.
      const installPaths = lazy.WindowsInstallsInfo.getInstallPaths(
        1,
        new Set([Services.dirsvc.get("GreBinD", Ci.nsIFile).path])
      );
      const msixInstalls = new Set();
      // We're just going to eat all errors here -- we don't want the event
      // to go unsent if we were unable to look for MSIX installs.
      try {
        wpm
          .findUserInstalledPackages(msixPackagePrefixes)
          .forEach(i => msixInstalls.add(i));
        if (pfn) {
          msixInstalls.delete(pfn);
        }
      } catch (ex) {}
      return {
        installPaths,
        msixInstalls,
      };
    }

    let extra = {};

    if (pfn) {
      if (lastInstallTime != null) {
        // We've already seen this install
        return {};
      }

      // First time seeing this install, record the timestamp.
      Services.prefs.setStringPref(TIMESTAMP_PREF, wpm.getInstalledDate());
      let install_data = getInstallData();

      installer_type = "msix";

      // Build the extra event data
      extra.version = AppConstants.MOZ_APP_VERSION;
      extra.build_id = AppConstants.MOZ_BUILDID;
      // The next few keys are static for the reasons described
      // No way to detect whether or not we were installed by an admin
      extra.admin_user = "false";
      // Always false at the moment, because we create a new profile
      // on first launch
      extra.profdir_existed = "false";
      // Obviously false for MSIX installs
      extra.from_msi = "false";
      // We have no way of knowing whether we were installed via the GUI,
      // through the command line, or some Enterprise management tool.
      extra.silent = "false";
      // There's no way to change the install path for an MSIX package
      extra.default_path = "true";
      extra.install_existed = install_data.msixInstalls.has(pfn).toString();
      install_data.msixInstalls.delete(pfn);
      extra.other_inst = (!!install_data.installPaths.size).toString();
      extra.other_msix_inst = (!!install_data.msixInstalls.size).toString();
    } else {
      let dataPath = dataPathOverride;
      if (!dataPath) {
        dataPath = Services.dirsvc.get("GreD", Ci.nsIFile);
        dataPath.append("installation_telemetry.json");
      }

      let dataBytes;
      try {
        dataBytes = await IOUtils.read(dataPath.path);
      } catch (ex) {
        if (ex.name == "NotFoundError") {
          // Many systems will not have the data file, return silently if not found as
          // there is nothing to record.
          return {};
        }
        throw ex;
      }
      const dataString = new TextDecoder("utf-16").decode(dataBytes);
      const data = JSON.parse(dataString);

      if (lastInstallTime && data.install_timestamp == lastInstallTime) {
        // We've already seen this install
        return {};
      }

      // First time seeing this install, record the timestamp.
      Services.prefs.setStringPref(TIMESTAMP_PREF, data.install_timestamp);
      let install_data = getInstallData();

      installer_type = data.installer_type;

      // Installation timestamp is not intended to be sent with telemetry,
      // remove it to emphasize this point.
      delete data.install_timestamp;

      // Build the extra event data
      extra.version = data.version;
      extra.build_id = data.build_id;
      extra.admin_user = data.admin_user.toString();
      extra.install_existed = data.install_existed.toString();
      extra.profdir_existed = data.profdir_existed.toString();
      extra.other_inst = (!!install_data.installPaths.size).toString();
      extra.other_msix_inst = (!!install_data.msixInstalls.size).toString();

      if (data.installer_type == "full") {
        extra.silent = data.silent.toString();
        extra.from_msi = data.from_msi.toString();
        extra.default_path = data.default_path.toString();
      }
    }
    return { installer_type, extra };
  },

  async reportInstallationTelemetry(
    dataPathOverride,
    msixPackagePrefixes = ["Mozilla.Firefox", "Mozilla.MozillaFirefox"]
  ) {
    // The optional dataPathOverride is only used for testing purposes.
    // Use this as a proxy for whether we're in a testing environment.
    // If we're in a testing environment we don't want to return the
    // same data even if we call this function multiple times in the
    // same instance.
    if (gInstallationTelemetryPromise && !dataPathOverride) {
      return gInstallationTelemetryPromise;
    }

    gInstallationTelemetryPromise = (async () => {
      let data = await BrowserUsageTelemetry.collectInstallationTelemetry(
        dataPathOverride,
        msixPackagePrefixes
      );

      if (data?.installer_type) {
        let { installer_type, extra } = data;

        // Record the event (mirrored to legacy telemetry using GIFFT)
        if (installer_type == "full") {
          Glean.installation.firstSeenFull.record(extra);
        } else if (installer_type == "stub") {
          Glean.installation.firstSeenStub.record(extra);
        } else if (installer_type == "msix") {
          Glean.installation.firstSeenMsix.record(extra);
        }

        // Scalars for the new-profile ping. We don't need to collect the build version
        // These are mirrored to legacy telemetry using GIFFT
        Glean.installationFirstSeen.installerType.set(installer_type);
        Glean.installationFirstSeen.version.set(extra.version);
        // Convert "true" or "false" strings back into booleans
        Glean.installationFirstSeen.adminUser.set(extra.admin_user === "true");
        Glean.installationFirstSeen.installExisted.set(
          extra.install_existed === "true"
        );
        Glean.installationFirstSeen.profdirExisted.set(
          extra.profdir_existed === "true"
        );
        Glean.installationFirstSeen.otherInst.set(extra.other_inst === "true");
        Glean.installationFirstSeen.otherMsixInst.set(
          extra.other_msix_inst === "true"
        );
        if (installer_type == "full") {
          Glean.installationFirstSeen.silent.set(extra.silent === "true");
          Glean.installationFirstSeen.fromMsi.set(extra.from_msi === "true");
          Glean.installationFirstSeen.defaultPath.set(
            extra.default_path === "true"
          );
        }
      }
      return data;
    })();

    return gInstallationTelemetryPromise;
  },
};

// Used by nsIBrowserUsage
export function getUniqueDomainsVisitedInPast24Hours() {
  return URICountListener.uniqueDomainsVisitedInPast24Hours;
}
