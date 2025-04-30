/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Current version of the format used by Session Restore.
const FORMAT_VERSION = 1;

const PERSIST_SESSIONS = Services.prefs.getBoolPref(
  "browser.sessionstore.persist_closed_tabs_between_sessions"
);
const TAB_CUSTOM_VALUES = new WeakMap();
const TAB_LAZY_STATES = new WeakMap();
const TAB_STATE_NEEDS_RESTORE = 1;
const TAB_STATE_RESTORING = 2;
const TAB_STATE_FOR_BROWSER = new WeakMap();
const WINDOW_RESTORE_IDS = new WeakMap();
const WINDOW_RESTORE_ZINDICES = new WeakMap();
const WINDOW_SHOWING_PROMISES = new Map();
const WINDOW_FLUSHING_PROMISES = new Map();

// A new window has just been restored. At this stage, tabs are generally
// not restored.
const NOTIFY_SINGLE_WINDOW_RESTORED = "sessionstore-single-window-restored";
const NOTIFY_WINDOWS_RESTORED = "sessionstore-windows-restored";
const NOTIFY_BROWSER_STATE_RESTORED = "sessionstore-browser-state-restored";
const NOTIFY_LAST_SESSION_CLEARED = "sessionstore-last-session-cleared";
const NOTIFY_LAST_SESSION_RE_ENABLED = "sessionstore-last-session-re-enable";
const NOTIFY_RESTORING_ON_STARTUP = "sessionstore-restoring-on-startup";
const NOTIFY_INITIATING_MANUAL_RESTORE =
  "sessionstore-initiating-manual-restore";
const NOTIFY_CLOSED_OBJECTS_CHANGED = "sessionstore-closed-objects-changed";
const NOTIFY_SAVED_TAB_GROUPS_CHANGED = "sessionstore-saved-tab-groups-changed";

const NOTIFY_TAB_RESTORED = "sessionstore-debug-tab-restored"; // WARNING: debug-only
const NOTIFY_DOMWINDOWCLOSED_HANDLED =
  "sessionstore-debug-domwindowclosed-handled"; // WARNING: debug-only

const NOTIFY_BROWSER_SHUTDOWN_FLUSH = "sessionstore-browser-shutdown-flush";

// Maximum number of tabs to restore simultaneously. Previously controlled by
// the browser.sessionstore.max_concurrent_tabs pref.
const MAX_CONCURRENT_TAB_RESTORES = 3;

// Minimum amount (in CSS px) by which we allow window edges to be off-screen
// when restoring a window, before we override the saved position to pull the
// window back within the available screen area.
const MIN_SCREEN_EDGE_SLOP = 8;

// global notifications observed
const OBSERVING = [
  "browser-window-before-show",
  "domwindowclosed",
  "quit-application-granted",
  "browser-lastwindow-close-granted",
  "quit-application",
  "browser:purge-session-history",
  "browser:purge-session-history-for-domain",
  "idle-daily",
  "clear-origin-attributes-data",
  "browsing-context-did-set-embedder",
  "browsing-context-discarded",
  "browser-shutdown-tabstate-updated",
];

// XUL Window properties to (re)store
// Restored in restoreDimensions()
const WINDOW_ATTRIBUTES = ["width", "height", "screenX", "screenY", "sizemode"];

const CHROME_FLAGS_MAP = [
  [Ci.nsIWebBrowserChrome.CHROME_TITLEBAR, "titlebar"],
  [Ci.nsIWebBrowserChrome.CHROME_WINDOW_CLOSE, "close"],
  [Ci.nsIWebBrowserChrome.CHROME_TOOLBAR, "toolbar"],
  [Ci.nsIWebBrowserChrome.CHROME_LOCATIONBAR, "location"],
  [Ci.nsIWebBrowserChrome.CHROME_PERSONAL_TOOLBAR, "personalbar"],
  [Ci.nsIWebBrowserChrome.CHROME_STATUSBAR, "status"],
  [Ci.nsIWebBrowserChrome.CHROME_MENUBAR, "menubar"],
  [Ci.nsIWebBrowserChrome.CHROME_WINDOW_RESIZE, "resizable"],
  [Ci.nsIWebBrowserChrome.CHROME_WINDOW_MINIMIZE, "minimizable"],
  [Ci.nsIWebBrowserChrome.CHROME_SCROLLBARS, "", "scrollbars=0"],
  [Ci.nsIWebBrowserChrome.CHROME_PRIVATE_WINDOW, "private"],
  [Ci.nsIWebBrowserChrome.CHROME_NON_PRIVATE_WINDOW, "non-private"],
  // Do not inherit remoteness and fissionness from the previous session.
  //[Ci.nsIWebBrowserChrome.CHROME_REMOTE_WINDOW, "remote", "non-remote"],
  //[Ci.nsIWebBrowserChrome.CHROME_FISSION_WINDOW, "fission", "non-fission"],
  // "chrome" and "suppressanimation" are always set.
  //[Ci.nsIWebBrowserChrome.CHROME_SUPPRESS_ANIMATION, "suppressanimation"],
  [Ci.nsIWebBrowserChrome.CHROME_ALWAYS_ON_TOP, "alwaysontop"],
  //[Ci.nsIWebBrowserChrome.CHROME_OPENAS_CHROME, "chrome", "chrome=0"],
  [Ci.nsIWebBrowserChrome.CHROME_EXTRA, "extrachrome"],
  [Ci.nsIWebBrowserChrome.CHROME_CENTER_SCREEN, "centerscreen"],
  [Ci.nsIWebBrowserChrome.CHROME_DEPENDENT, "dependent"],
  [Ci.nsIWebBrowserChrome.CHROME_MODAL, "modal"],
  [Ci.nsIWebBrowserChrome.CHROME_OPENAS_DIALOG, "dialog", "dialog=0"],
];

// Hideable window features to (re)store
// Restored in restoreWindowFeatures()
const WINDOW_HIDEABLE_FEATURES = [
  "menubar",
  "toolbar",
  "locationbar",
  "personalbar",
  "statusbar",
  "scrollbars",
];

const WINDOW_OPEN_FEATURES_MAP = {
  locationbar: "location",
  statusbar: "status",
};

// These are tab events that we listen to.
const TAB_EVENTS = [
  "TabOpen",
  "TabBrowserInserted",
  "TabClose",
  "TabSelect",
  "TabShow",
  "TabHide",
  "TabPinned",
  "TabUnpinned",
  "TabGroupCreate",
  "TabGroupRemoveRequested",
  "TabGroupRemoved",
  "TabGrouped",
  "TabUngrouped",
  "TabGroupCollapse",
  "TabGroupExpand",
];

const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

/**
 * When calling restoreTabContent, we can supply a reason why
 * the content is being restored. These are those reasons.
 */
const RESTORE_TAB_CONTENT_REASON = {
  /**
   * SET_STATE:
   * We're restoring this tab's content because we're setting
   * state inside this browser tab, probably because the user
   * has asked us to restore a tab (or window, or entire session).
   */
  SET_STATE: 0,
  /**
   * NAVIGATE_AND_RESTORE:
   * We're restoring this tab's content because a navigation caused
   * us to do a remoteness-flip.
   */
  NAVIGATE_AND_RESTORE: 1,
};

// 'browser.startup.page' preference value to resume the previous session.
const BROWSER_STARTUP_RESUME_SESSION = 3;

// Used by SessionHistoryListener.
const kNoIndex = Number.MAX_SAFE_INTEGER;
const kLastIndex = Number.MAX_SAFE_INTEGER - 1;

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";

import { TabMetrics } from "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs";
import { TelemetryTimestamps } from "resource://gre/modules/TelemetryTimestamps.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { GlobalState } from "resource:///modules/sessionstore/GlobalState.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetters(lazy, {
  gScreenManager: ["@mozilla.org/gfx/screenmanager;1", "nsIScreenManager"],
});

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  DevToolsShim: "chrome://devtools-startup/content/DevToolsShim.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  PrivacyFilter: "resource://gre/modules/sessionstore/PrivacyFilter.sys.mjs",
  sessionStoreLogger: "resource:///modules/sessionstore/SessionLogger.sys.mjs",
  RunState: "resource:///modules/sessionstore/RunState.sys.mjs",
  SessionCookies: "resource:///modules/sessionstore/SessionCookies.sys.mjs",
  SessionFile: "resource:///modules/sessionstore/SessionFile.sys.mjs",
  SessionHistory: "resource://gre/modules/sessionstore/SessionHistory.sys.mjs",
  SessionSaver: "resource:///modules/sessionstore/SessionSaver.sys.mjs",
  SessionStartup: "resource:///modules/sessionstore/SessionStartup.sys.mjs",
  SessionStoreHelper:
    "resource://gre/modules/sessionstore/SessionStoreHelper.sys.mjs",
  TabAttributes: "resource:///modules/sessionstore/TabAttributes.sys.mjs",
  TabCrashHandler: "resource:///modules/ContentCrashHandlers.sys.mjs",
  TabGroupState: "resource:///modules/sessionstore/TabGroupState.sys.mjs",
  TabState: "resource:///modules/sessionstore/TabState.sys.mjs",
  TabStateCache: "resource:///modules/sessionstore/TabStateCache.sys.mjs",
  TabStateFlusher: "resource:///modules/sessionstore/TabStateFlusher.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "blankURI", () => {
  return Services.io.newURI("about:blank");
});

/**
 * |true| if we are in debug mode, |false| otherwise.
 * Debug mode is controlled by preference browser.sessionstore.debug
 */
var gDebuggingEnabled = false;

/**
 * @namespace SessionStore
 */
export var SessionStore = {
  get logger() {
    return SessionStoreInternal._log;
  },
  get promiseInitialized() {
    return SessionStoreInternal.promiseInitialized;
  },

  get promiseAllWindowsRestored() {
    return SessionStoreInternal.promiseAllWindowsRestored;
  },

  get canRestoreLastSession() {
    return SessionStoreInternal.canRestoreLastSession;
  },

  set canRestoreLastSession(val) {
    SessionStoreInternal.canRestoreLastSession = val;
  },

  get lastClosedObjectType() {
    return SessionStoreInternal.lastClosedObjectType;
  },

  get lastClosedActions() {
    return [...SessionStoreInternal._lastClosedActions];
  },

  get LAST_ACTION_CLOSED_TAB() {
    return SessionStoreInternal._LAST_ACTION_CLOSED_TAB;
  },

  get LAST_ACTION_CLOSED_WINDOW() {
    return SessionStoreInternal._LAST_ACTION_CLOSED_WINDOW;
  },

  get savedGroups() {
    return SessionStoreInternal._savedGroups;
  },

  get willAutoRestore() {
    return SessionStoreInternal.willAutoRestore;
  },

  get shouldRestoreLastSession() {
    return SessionStoreInternal._shouldRestoreLastSession;
  },

  init: function ss_init() {
    SessionStoreInternal.init();
  },

  /**
   * Get the collection of all matching windows tracked by SessionStore
   * @param {Window|Object} [aWindowOrOptions] Optionally an options object or a window to used to determine if we're filtering for private or non-private windows
   * @param {boolean} [aWindowOrOptions.private] Determine if we should filter for private or non-private windows
   */
  getWindows(aWindowOrOptions) {
    return SessionStoreInternal.getWindows(aWindowOrOptions);
  },

  /**
   * Get window a given closed tab belongs to
   * @param {integer} aClosedId The closedId of the tab whose window we want to find
   * @param {boolean} [aIncludePrivate] Optionally include private windows when searching for the closed tab
   */
  getWindowForTabClosedId(aClosedId, aIncludePrivate) {
    return SessionStoreInternal.getWindowForTabClosedId(
      aClosedId,
      aIncludePrivate
    );
  },

  getBrowserState: function ss_getBrowserState() {
    return SessionStoreInternal.getBrowserState();
  },

  setBrowserState: function ss_setBrowserState(aState) {
    SessionStoreInternal.setBrowserState(aState);
  },

  getWindowState: function ss_getWindowState(aWindow) {
    return SessionStoreInternal.getWindowState(aWindow);
  },

  setWindowState: function ss_setWindowState(aWindow, aState, aOverwrite) {
    SessionStoreInternal.setWindowState(aWindow, aState, aOverwrite);
  },

  getTabState: function ss_getTabState(aTab) {
    return SessionStoreInternal.getTabState(aTab);
  },

  setTabState: function ss_setTabState(aTab, aState) {
    SessionStoreInternal.setTabState(aTab, aState);
  },

  // Return whether a tab is restoring.
  isTabRestoring(aTab) {
    return TAB_STATE_FOR_BROWSER.has(aTab.linkedBrowser);
  },

  getInternalObjectState(obj) {
    return SessionStoreInternal.getInternalObjectState(obj);
  },

  duplicateTab: function ss_duplicateTab(
    aWindow,
    aTab,
    aDelta = 0,
    aRestoreImmediately = true,
    aOptions = {}
  ) {
    return SessionStoreInternal.duplicateTab(
      aWindow,
      aTab,
      aDelta,
      aRestoreImmediately,
      aOptions
    );
  },

  /**
   * How many tabs were last closed. If multiple tabs were selected and closed together,
   * we'll return that number. Normally the count is 1, or 0 if no tabs have been
   * recently closed in this window.
   * @returns the number of tabs that were last closed.
   */
  getLastClosedTabCount(aWindow) {
    return SessionStoreInternal.getLastClosedTabCount(aWindow);
  },

  resetLastClosedTabCount(aWindow) {
    SessionStoreInternal.resetLastClosedTabCount(aWindow);
  },

  /**
   * Get the number of closed tabs associated with a specific window
   * @param {Window} aWindow
   */
  getClosedTabCountForWindow: function ss_getClosedTabCountForWindow(aWindow) {
    return SessionStoreInternal.getClosedTabCountForWindow(aWindow);
  },

  /**
   * Get the number of closed tabs associated with all matching windows
   * @param {Window|Object} [aOptions]
   *        Either a DOMWindow (see aOptions.sourceWindow) or an object with properties
            to identify which closed tabs to include in the count.
   * @param {Window} aOptions.sourceWindow
            A browser window used to identity privateness.
            When closedTabsFromAllWindows is false, we only count closed tabs assocated with this window.
   * @param {boolean} [aOptions.private = false]
            Explicit indicator to constrain tab count to only private or non-private windows,
   * @param {boolean} [aOptions.closedTabsFromAllWindows]
            Override the value of the closedTabsFromAllWindows preference.
   * @param {boolean} [aOptions.closedTabsFromClosedWindows]
            Override the value of the closedTabsFromClosedWindows preference.
   */
  getClosedTabCount: function ss_getClosedTabCount(aOptions) {
    return SessionStoreInternal.getClosedTabCount(aOptions);
  },

  /**
   * Get the number of closed tabs from recently closed window
   *
   * This is normally only relevant in a non-private window context, as we don't
   * keep data from closed private windows.
   */
  getClosedTabCountFromClosedWindows:
    function ss_getClosedTabCountFromClosedWindows() {
      return SessionStoreInternal.getClosedTabCountFromClosedWindows();
    },

  /**
   * Get the closed tab data associated with this window
   * @param {Window} aWindow
   */
  getClosedTabDataForWindow: function ss_getClosedTabDataForWindow(aWindow) {
    return SessionStoreInternal.getClosedTabDataForWindow(aWindow);
  },

  /**
   * Get the closed tab data associated with all matching windows
   * @param {Window|Object} [aOptions]
   *        Either a DOMWindow (see aOptions.sourceWindow) or an object with properties
            to identify which closed tabs to get data from
   * @param {Window} aOptions.sourceWindow
            A browser window used to identity privateness.
            When closedTabsFromAllWindows is false, we only include closed tabs assocated with this window.
   * @param {boolean} [aOptions.private = false]
            Explicit indicator to constrain tab data to only private or non-private windows,
   * @param {boolean} [aOptions.closedTabsFromAllWindows]
            Override the value of the closedTabsFromAllWindows preference.
   * @param {boolean} [aOptions.closedTabsFromClosedWindows]
            Override the value of the closedTabsFromClosedWindows preference.
   */
  getClosedTabData: function ss_getClosedTabData(aOptions) {
    return SessionStoreInternal.getClosedTabData(aOptions);
  },

  /**
   * Get the closed tab data associated with all closed windows
   * @returns an un-sorted array of tabData for closed tabs from closed windows
   */
  getClosedTabDataFromClosedWindows:
    function ss_getClosedTabDataFromClosedWindows() {
      return SessionStoreInternal.getClosedTabDataFromClosedWindows();
    },

  /**
   * Get the closed tab group data associated with all matching windows
   * @param {Window|object} aOptions
   *        Either a DOMWindow (see aOptions.sourceWindow) or an object with properties
            to identify the window source of the closed tab groups
   * @param {Window} [aOptions.sourceWindow]
            A browser window used to identity privateness.
            When closedTabsFromAllWindows is false, we only include closed tab groups assocated with this window.
   * @param {boolean} [aOptions.private = false]
            Explicit indicator to constrain tab group data to only private or non-private windows,
   * @param {boolean} [aOptions.closedTabsFromAllWindows]
            Override the value of the closedTabsFromAllWindows preference.
   * @param {boolean} [aOptions.closedTabsFromClosedWindows]
            Override the value of the closedTabsFromClosedWindows preference.
   * @returns {ClosedTabGroupStateData[]}
   */
  getClosedTabGroups: function ss_getClosedTabGroups(aOptions) {
    return SessionStoreInternal.getClosedTabGroups(aOptions);
  },

  /**
   * Get the last closed tab ID associated with a specific window
   * @param {Window} aWindow
   */
  getLastClosedTabGroupId(window) {
    return SessionStoreInternal.getLastClosedTabGroupId(window);
  },

  /**
   * Re-open a closed tab
   * @param {Window|Object} aSource
   *        Either a DOMWindow or an object with properties to resolve to the window
   *        the tab was previously open in.
   * @param {String} aSource.sourceWindowId
            A SessionStore window id used to look up the window where the tab was closed
   * @param {number} aSource.sourceClosedId
            The closedId used to look up the closed window where the tab was closed
   * @param {Integer} [aIndex = 0]
   *        The index of the tab in the closedTabs array (via SessionStore.getClosedTabData), where 0 is most recent.
   * @param {Window} [aTargetWindow = aWindow] Optional window to open the tab into, defaults to current (topWindow).
   * @returns a reference to the reopened tab.
   */
  undoCloseTab: function ss_undoCloseTab(aSource, aIndex, aTargetWindow) {
    return SessionStoreInternal.undoCloseTab(aSource, aIndex, aTargetWindow);
  },

  /**
   * Re-open a tab from a closed window, which corresponds to the closedId
   * @param {Window|Object} aSource
   *        Either a DOMWindow or an object with properties to resolve to the window
   *        the tab was previously open in.
   * @param {String} aSource.sourceWindowId
            A SessionStore window id used to look up the window where the tab was closed
   * @param {number} aSource.sourceClosedId
            The closedId used to look up the closed window where the tab was closed
   * @param {integer} aClosedId
   *        The closedId of the tab or window
   * @param {Window} [aTargetWindow = aWindow] Optional window to open the tab into, defaults to current (topWindow).
   * @returns a reference to the reopened tab.
   */
  undoClosedTabFromClosedWindow: function ss_undoClosedTabFromClosedWindow(
    aSource,
    aClosedId,
    aTargetWindow
  ) {
    return SessionStoreInternal.undoClosedTabFromClosedWindow(
      aSource,
      aClosedId,
      aTargetWindow
    );
  },

  /**
   * Forget a closed tab associated with a given window
   * Removes the record at the given index so it cannot be un-closed or appear
   * in a list of recently-closed tabs
   *
   * @param {Window|Object} aSource
   *        Either a DOMWindow or an object with properties to resolve to the window
   *        the tab was previously open in.
   * @param {String} aSource.sourceWindowId
            A SessionStore window id used to look up the window where the tab was closed
   * @param {number} aSource.sourceClosedId
            The closedId used to look up the closed window where the tab was closed
   * @param {Integer} [aIndex = 0]
   *        The index into the window's list of closed tabs
   * @throws {InvalidArgumentError} if the window is not tracked by SessionStore, or index is out of bounds
   */
  forgetClosedTab: function ss_forgetClosedTab(aSource, aIndex) {
    return SessionStoreInternal.forgetClosedTab(aSource, aIndex);
  },

  /**
   * Forget a closed tab group associated with a given window
   * Removes the record at the given index so it cannot be un-closed or appear
   * in a list of recently-closed tabs
   *
   * @param {Window|Object} aSource
   *        Either a DOMWindow or an object with properties to resolve to the window
   *        the tab was previously open in.
   * @param {String} aSource.sourceWindowId
            A SessionStore window id used to look up the window where the tab group was closed
   * @param {number} aSource.sourceClosedId
            The closedId used to look up the closed window where the tab group was closed
   * @param {string} tabGroupId
   *        The tab group ID of the closed tab group
   * @throws {InvalidArgumentError}
   *        if the window or tab group is not tracked by SessionStore
   */
  forgetClosedTabGroup: function ss_forgetClosedTabGroup(aSource, tabGroupId) {
    return SessionStoreInternal.forgetClosedTabGroup(aSource, tabGroupId);
  },

  /**
   * Forget a closed tab that corresponds to the closedId
   * Removes the record with this closedId so it cannot be un-closed or appear
   * in a list of recently-closed tabs
   *
   * @param {integer} aClosedId
   *        The closedId of the tab
   * @param {Window|Object} aSourceOptions
   *        Either a DOMWindow or an object with properties to resolve to the window
   *        the tab was previously open in.
   * @param {boolean} [aSourceOptions.includePrivate = true]
            If no other means of resolving a source window is given, this flag is used to
            constrain a search across all open window's closed tabs.
   * @param {String} aSourceOptions.sourceWindowId
            A SessionStore window id used to look up the window where the tab was closed
   * @param {number} aSourceOptions.sourceClosedId
            The closedId used to look up the closed window where the tab was closed
   * @throws {InvalidArgumentError} if the closedId doesnt match a closed tab in any window
   */
  forgetClosedTabById: function ss_forgetClosedTabById(
    aClosedId,
    aSourceOptions
  ) {
    SessionStoreInternal.forgetClosedTabById(aClosedId, aSourceOptions);
  },

  /**
   * Forget a closed window.
   * Removes the record with this closedId so it cannot be un-closed or appear
   * in a list of recently-closed windows
   *
   * @param {integer} aClosedId
   *        The closedId of the window
   * @throws {InvalidArgumentError} if the closedId doesnt match a closed window
   */
  forgetClosedWindowById: function ss_forgetClosedWindowById(aClosedId) {
    SessionStoreInternal.forgetClosedWindowById(aClosedId);
  },

  /**
   * Look up the object type ("tab" or "window") for a given closedId
   * @param {integer} aClosedId
   */
  getObjectTypeForClosedId(aClosedId) {
    return SessionStoreInternal.getObjectTypeForClosedId(aClosedId);
  },

  /**
   * Look up a window tracked by SessionStore by its id
   * @param {String} aSessionStoreId
   */
  getWindowById: function ss_getWindowById(aSessionStoreId) {
    return SessionStoreInternal.getWindowById(aSessionStoreId);
  },

  getClosedWindowCount: function ss_getClosedWindowCount() {
    return SessionStoreInternal.getClosedWindowCount();
  },

  // this should only be used by one caller (currently restoreLastClosedTabOrWindowOrSession in browser.js)
  popLastClosedAction: function ss_popLastClosedAction() {
    return SessionStoreInternal._lastClosedActions.pop();
  },

  // for testing purposes
  resetLastClosedActions: function ss_resetLastClosedActions() {
    SessionStoreInternal._lastClosedActions = [];
  },

  getClosedWindowData: function ss_getClosedWindowData() {
    return SessionStoreInternal.getClosedWindowData();
  },

  maybeDontRestoreTabs(aWindow) {
    SessionStoreInternal.maybeDontRestoreTabs(aWindow);
  },

  undoCloseWindow: function ss_undoCloseWindow(aIndex) {
    return SessionStoreInternal.undoCloseWindow(aIndex);
  },

  forgetClosedWindow: function ss_forgetClosedWindow(aIndex) {
    return SessionStoreInternal.forgetClosedWindow(aIndex);
  },

  getCustomWindowValue(aWindow, aKey) {
    return SessionStoreInternal.getCustomWindowValue(aWindow, aKey);
  },

  setCustomWindowValue(aWindow, aKey, aStringValue) {
    SessionStoreInternal.setCustomWindowValue(aWindow, aKey, aStringValue);
  },

  deleteCustomWindowValue(aWindow, aKey) {
    SessionStoreInternal.deleteCustomWindowValue(aWindow, aKey);
  },

  getCustomTabValue(aTab, aKey) {
    return SessionStoreInternal.getCustomTabValue(aTab, aKey);
  },

  setCustomTabValue(aTab, aKey, aStringValue) {
    SessionStoreInternal.setCustomTabValue(aTab, aKey, aStringValue);
  },

  deleteCustomTabValue(aTab, aKey) {
    SessionStoreInternal.deleteCustomTabValue(aTab, aKey);
  },

  getLazyTabValue(aTab, aKey) {
    return SessionStoreInternal.getLazyTabValue(aTab, aKey);
  },

  getCustomGlobalValue(aKey) {
    return SessionStoreInternal.getCustomGlobalValue(aKey);
  },

  setCustomGlobalValue(aKey, aStringValue) {
    SessionStoreInternal.setCustomGlobalValue(aKey, aStringValue);
  },

  deleteCustomGlobalValue(aKey) {
    SessionStoreInternal.deleteCustomGlobalValue(aKey);
  },

  restoreLastSession: function ss_restoreLastSession() {
    SessionStoreInternal.restoreLastSession();
  },

  speculativeConnectOnTabHover(tab) {
    SessionStoreInternal.speculativeConnectOnTabHover(tab);
  },

  getCurrentState(aUpdateAll) {
    return SessionStoreInternal.getCurrentState(aUpdateAll);
  },

  reviveCrashedTab(aTab) {
    return SessionStoreInternal.reviveCrashedTab(aTab);
  },

  reviveAllCrashedTabs() {
    return SessionStoreInternal.reviveAllCrashedTabs();
  },

  updateSessionStoreFromTablistener(
    aBrowser,
    aBrowsingContext,
    aPermanentKey,
    aData,
    aForStorage
  ) {
    return SessionStoreInternal.updateSessionStoreFromTablistener(
      aBrowser,
      aBrowsingContext,
      aPermanentKey,
      aData,
      aForStorage
    );
  },

  getSessionHistory(tab, updatedCallback) {
    return SessionStoreInternal.getSessionHistory(tab, updatedCallback);
  },

  /**
   * Re-open a tab or window which corresponds to the closedId
   *
   * @param {integer} aClosedId
   *        The closedId of the tab or window
   * @param {boolean} [aIncludePrivate = true]
   *        Whether to match the aClosedId to only closed private tabs/windows or non-private
   * @param {Window} [aTargetWindow]
   *        When aClosedId is for a closed tab, which window to re-open the tab into.
   *        Defaults to current (topWindow).
   *
   * @returns a tab or window object
   */
  undoCloseById(aClosedId, aIncludePrivate, aTargetWindow) {
    return SessionStoreInternal.undoCloseById(
      aClosedId,
      aIncludePrivate,
      aTargetWindow
    );
  },

  resetBrowserToLazyState(tab) {
    return SessionStoreInternal.resetBrowserToLazyState(tab);
  },

  maybeExitCrashedState(browser) {
    SessionStoreInternal.maybeExitCrashedState(browser);
  },

  isBrowserInCrashedSet(browser) {
    return SessionStoreInternal.isBrowserInCrashedSet(browser);
  },

  // this is used for testing purposes
  resetNextClosedId() {
    SessionStoreInternal._nextClosedId = 0;
  },

  /**
   * Ensures that session store has registered and started tracking a given window.
   * @param window
   *        Window reference
   */
  ensureInitialized(window) {
    if (SessionStoreInternal._sessionInitialized && !window.__SSi) {
      /*
        We need to check that __SSi is not defined on the window so that if
        onLoad function is in the middle of executing we don't enter the function
        again and try to redeclare the ContentSessionStore script.
       */
      SessionStoreInternal.onLoad(window);
    }
  },

  getCurrentEpoch(browser) {
    return SessionStoreInternal.getCurrentEpoch(browser.permanentKey);
  },

  /**
   * Determines whether the passed version number is compatible with
   * the current version number of the SessionStore.
   *
   * @param version The format and version of the file, as an array, e.g.
   * ["sessionrestore", 1]
   */
  isFormatVersionCompatible(version) {
    if (!version) {
      return false;
    }
    if (!Array.isArray(version)) {
      // Improper format.
      return false;
    }
    if (version[0] != "sessionrestore") {
      // Not a Session Restore file.
      return false;
    }
    let number = Number.parseFloat(version[1]);
    if (Number.isNaN(number)) {
      return false;
    }
    return number <= FORMAT_VERSION;
  },

  /**
   * Filters out not worth-saving tabs from a given browser state object.
   *
   * @param aState (object)
   *        The browser state for which we remove worth-saving tabs.
   *        The given object will be modified.
   */
  keepOnlyWorthSavingTabs(aState) {
    let closedWindowShouldRestore = null;
    for (let i = aState.windows.length - 1; i >= 0; i--) {
      let win = aState.windows[i];
      for (let j = win.tabs.length - 1; j >= 0; j--) {
        let tab = win.tabs[j];
        if (!SessionStoreInternal._shouldSaveTab(tab)) {
          win.tabs.splice(j, 1);
          if (win.selected > j) {
            win.selected--;
          }
        }
      }

      // If it's the last window (and no closedWindow that will restore), keep the window state with no tabs.
      if (
        !win.tabs.length &&
        (aState.windows.length > 1 ||
          closedWindowShouldRestore ||
          (closedWindowShouldRestore == null &&
            (closedWindowShouldRestore = aState._closedWindows.some(
              w => w._shouldRestore
            ))))
      ) {
        aState.windows.splice(i, 1);
        if (aState.selectedWindow > i) {
          aState.selectedWindow--;
        }
      }
    }
  },

  /**
   * Clear session store data for a given private browsing window.
   * @param {ChromeWindow} win - Open private browsing window to clear data for.
   */
  purgeDataForPrivateWindow(win) {
    return SessionStoreInternal.purgeDataForPrivateWindow(win);
  },

  /**
   * Add a tab group to the session's saved group list.
   * @param {MozTabbrowserTabGroup} tabGroup - The group to save
   */
  addSavedTabGroup(tabGroup) {
    return SessionStoreInternal.addSavedTabGroup(tabGroup);
  },

  /**
   * Retrieve the tab group state of a saved tab group by ID.
   *
   * @param {string} tabGroupId
   * @returns {SavedTabGroupStateData|undefined}
   */
  getSavedTabGroup(tabGroupId) {
    return SessionStoreInternal.getSavedTabGroup(tabGroupId);
  },

  /**
   * Returns all tab groups that were saved in this session.
   * @returns {SavedTabGroupStateData[]}
   */
  getSavedTabGroups() {
    return SessionStoreInternal.getSavedTabGroups();
  },

  /**
   * Remove a tab group from the session's saved tab group list.
   * @param {string} tabGroupId
   *   The ID of the tab group to remove
   */
  forgetSavedTabGroup(tabGroupId) {
    return SessionStoreInternal.forgetSavedTabGroup(tabGroupId);
  },

  /**
   * Re-open a closed tab group
   * @param {Window|Object} source
   *        Either a DOMWindow or an object with properties to resolve to the window
   *        the tab was previously open in.
   * @param {string} source.sourceWindowId
            A SessionStore window id used to look up the window where the tab was closed.
   * @param {number} source.sourceClosedId
            The closedId used to look up the closed window where the tab was closed.
   * @param {string} tabGroupId
   *        The unique ID of the group to restore.
   * @param {Window} [targetWindow] defaults to the top window if not specified.
   * @returns {MozTabbrowserTabGroup}
   *   a reference to the restored tab group in a browser window.
   */
  undoCloseTabGroup(source, tabGroupId, targetWindow) {
    return SessionStoreInternal.undoCloseTabGroup(
      source,
      tabGroupId,
      targetWindow
    );
  },

  /**
   * Re-open a saved tab group.
   * Note that this method does not require passing a window source, as saved
   * tab groups are independent of windows.
   * Attempting to open a saved tab group in a private window will raise an error.
   * @param {string} tabGroupId
   *        The unique ID of the group to restore.
   * @param {Window} [targetWindow] defaults to the top window if not specified.
   * @returns {MozTabbrowserTabGroup}
   *   a reference to the restored tab group in a browser window.
   */
  openSavedTabGroup(
    tabGroupId,
    targetWindow,
    { source = TabMetrics.METRIC_SOURCE.UNKNOWN } = {}
  ) {
    let isVerticalMode = targetWindow.gBrowser.tabContainer.verticalMode;
    Glean.tabgroup.reopen.record({
      id: tabGroupId,
      source,
      layout: isVerticalMode
        ? TabMetrics.METRIC_TABS_LAYOUT.VERTICAL
        : TabMetrics.METRIC_TABS_LAYOUT.HORIZONTAL,
      type: TabMetrics.METRIC_REOPEN_TYPE.SAVED,
    });
    if (source == TabMetrics.METRIC_SOURCE.SUGGEST) {
      Glean.tabgroup.groupInteractions.open_suggest.add(1);
    } else if (source == TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU) {
      Glean.tabgroup.groupInteractions.open_tabmenu.add(1);
    } else if (source == TabMetrics.METRIC_SOURCE.RECENT_TABS) {
      Glean.tabgroup.groupInteractions.open_recent.add(1);
    }

    return SessionStoreInternal.openSavedTabGroup(tabGroupId, targetWindow);
  },

  /**
   * Determine whether a group is saveable, based on whether any of its tabs
   * are saveable per ssi_shouldSaveTabState.
   * @param {MozTabbrowserTabGroup} group the tab group to check
   * @returns {boolean} true if the group can be saved, false if it should
   *  be discarded.
   */
  shouldSaveTabGroup(group) {
    return SessionStoreInternal.shouldSaveTabGroup(group);
  },
};

// Freeze the SessionStore object. We don't want anyone to modify it.
Object.freeze(SessionStore);

/**
 * @namespace SessionStoreInternal
 *
 * @description Internal implementations and helpers for the public SessionStore methods
 */
var SessionStoreInternal = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  _globalState: new GlobalState(),

  // A counter to be used to generate a unique ID for each closed tab or window.
  _nextClosedId: 0,

  // During the initial restore and setBrowserState calls tracks the number of
  // windows yet to be restored
  _restoreCount: -1,

  // For each <browser> element, records the SHistoryListener.
  _browserSHistoryListener: new WeakMap(),

  // Tracks the various listeners that are used throughout the restore.
  _restoreListeners: new WeakMap(),

  // Records the promise created in _restoreHistory, which is used to track
  // the completion of the first phase of the restore.
  _tabStateRestorePromises: new WeakMap(),

  // The history data needed to be restored in the parent.
  _tabStateToRestore: new WeakMap(),

  // For each <browser> element, records the current epoch.
  _browserEpochs: new WeakMap(),

  // Any browsers that fires the oop-browser-crashed event gets stored in
  // here - that way we know which browsers to ignore messages from (until
  // they get restored).
  _crashedBrowsers: new WeakSet(),

  // A map (xul:browser -> FrameLoader) that maps a browser to the last
  // associated frameLoader we heard about.
  _lastKnownFrameLoader: new WeakMap(),

  // A map (xul:browser -> object) that maps a browser associated with a
  // recently closed tab to all its necessary state information we need to
  // properly handle final update message.
  _closingTabMap: new WeakMap(),

  // A map (xul:browser -> object) that maps a browser associated with a
  // recently closed tab due to a window closure to the tab state information
  // that is being stored in _closedWindows for that tab.
  _tabClosingByWindowMap: new WeakMap(),

  // A set of window data that has the potential to be saved in the _closedWindows
  // array for the session. We will remove window data from this set whenever
  // forgetClosedWindow is called for the window, or when session history is
  // purged, so that we don't accidentally save that data after the flush has
  // completed. Closed tabs use a more complicated mechanism for this particular
  // problem. When forgetClosedTab is called, the browser is removed from the
  // _closingTabMap, so its data is not recorded. In the purge history case,
  // the closedTabs array per window is overwritten so that once the flush is
  // complete, the tab would only ever add itself to an array that SessionStore
  // no longer cares about. Bug 1230636 has been filed to make the tab case
  // work more like the window case, which is more explicit, and easier to
  // reason about.
  _saveableClosedWindowData: new WeakSet(),

  // whether a setBrowserState call is in progress
  _browserSetState: false,

  // time in milliseconds when the session was started (saved across sessions),
  // defaults to now if no session was restored or timestamp doesn't exist
  _sessionStartTime: Date.now(),

  /**
   * states for all currently opened windows
   * @type {object.<WindowID, WindowStateData>}
   */
  _windows: {},

  // counter for creating unique window IDs
  _nextWindowID: 0,

  // states for all recently closed windows
  _closedWindows: [],

  /** @type {SavedTabGroupStateData[]} states for all saved+closed tab groups */
  _savedGroups: [],

  // collection of session states yet to be restored
  _statesToRestore: {},

  // counts the number of crashes since the last clean start
  _recentCrashes: 0,

  // whether the last window was closed and should be restored
  _restoreLastWindow: false,

  // whether we should restore last session on the next launch
  // of a regular Firefox window. This scenario is triggered
  // when a user closes all regular Firefox windows but the session is not over
  _shouldRestoreLastSession: false,

  // whether we will potentially be restoring the session
  // more than once without Firefox restarting in between
  _restoreWithoutRestart: false,

  // number of tabs currently restoring
  _tabsRestoringCount: 0,

  /**
   * @typedef {Object} CloseAction
   * @property {string} type
   *   What the close action acted upon. One of either _LAST_ACTION_CLOSED_TAB or
   *   _LAST_ACTION_CLOSED_WINDOW
   * @property {number} closedId
   *   The unique ID of the item that closed.
   */

  /**
   * An in-order stack of close actions for tabs and windows.
   * @type {CloseAction[]}
   */
  _lastClosedActions: [],

  /**
   * Removes an object from the _lastClosedActions list
   *
   * @param closedAction
   *        Either _LAST_ACTION_CLOSED_TAB or _LAST_ACTION_CLOSED_WINDOW
   * @param {integer} closedId
   *        The closedId of a tab or window
   */
  _removeClosedAction(closedAction, closedId) {
    let closedActionIndex = this._lastClosedActions.findIndex(
      obj => obj.type == closedAction && obj.closedId == closedId
    );

    if (closedActionIndex > -1) {
      this._lastClosedActions.splice(closedActionIndex, 1);
    }
  },

  /**
   * Add an object to the _lastClosedActions list and truncates the list if needed
   *
   * @param closedAction
   *        Either _LAST_ACTION_CLOSED_TAB or _LAST_ACTION_CLOSED_WINDOW
   * @param {integer} closedId
   *        The closedId of a tab or window
   */
  _addClosedAction(closedAction, closedId) {
    this._lastClosedActions.push({
      type: closedAction,
      closedId,
    });
    let maxLength = this._max_tabs_undo * this._max_windows_undo;

    if (this._lastClosedActions.length > maxLength) {
      this._lastClosedActions = this._lastClosedActions.slice(-maxLength);
    }
  },

  _LAST_ACTION_CLOSED_TAB: "tab",

  _LAST_ACTION_CLOSED_WINDOW: "window",

  _log: null,

  // When starting Firefox with a single private window or web app window, this is the place
  // where we keep the session we actually wanted to restore in case the user
  // decides to later open a non-private window as well.
  _deferredInitialState: null,

  // Keeps track of whether a notification needs to be sent that closed objects have changed.
  _closedObjectsChanged: false,

  // A promise resolved once initialization is complete
  _deferredInitialized: (function () {
    let deferred = {};

    deferred.promise = new Promise((resolve, reject) => {
      deferred.resolve = resolve;
      deferred.reject = reject;
    });

    return deferred;
  })(),

  // Whether session has been initialized
  _sessionInitialized: false,

  // A promise resolved once all windows are restored.
  _deferredAllWindowsRestored: (function () {
    let deferred = {};

    deferred.promise = new Promise((resolve, reject) => {
      deferred.resolve = resolve;
      deferred.reject = reject;
    });

    return deferred;
  })(),

  get promiseAllWindowsRestored() {
    return this._deferredAllWindowsRestored.promise;
  },

  // Promise that is resolved when we're ready to initialize
  // and restore the session.
  _promiseReadyForInitialization: null,

  // Keep busy state counters per window.
  _windowBusyStates: new WeakMap(),

  /**
   * A promise fulfilled once initialization is complete.
   */
  get promiseInitialized() {
    return this._deferredInitialized.promise;
  },

  get canRestoreLastSession() {
    return LastSession.canRestore;
  },

  set canRestoreLastSession(val) {
    // Cheat a bit; only allow false.
    if (!val) {
      LastSession.clear();
    }
  },

  /**
   * Returns a string describing the last closed object, either "tab" or "window".
   *
   * This was added to support the sessions.restore WebExtensions API.
   */
  get lastClosedObjectType() {
    if (this._closedWindows.length) {
      // Since there are closed windows, we need to check if there's a closed tab
      // in one of the currently open windows that was closed after the
      // last-closed window.
      let tabTimestamps = [];
      for (let window of Services.wm.getEnumerator("navigator:browser")) {
        let windowState = this._windows[window.__SSi];
        if (windowState && windowState._closedTabs[0]) {
          tabTimestamps.push(windowState._closedTabs[0].closedAt);
        }
      }
      if (
        !tabTimestamps.length ||
        tabTimestamps.sort((a, b) => b - a)[0] < this._closedWindows[0].closedAt
      ) {
        return this._LAST_ACTION_CLOSED_WINDOW;
      }
    }
    return this._LAST_ACTION_CLOSED_TAB;
  },

  /**
   * Returns a boolean that determines whether the session will be automatically
   * restored upon the _next_ startup or a restart.
   */
  get willAutoRestore() {
    return (
      !PrivateBrowsingUtils.permanentPrivateBrowsing &&
      (Services.prefs.getBoolPref("browser.sessionstore.resume_session_once") ||
        Services.prefs.getIntPref("browser.startup.page") ==
          BROWSER_STARTUP_RESUME_SESSION)
    );
  },

  /**
   * Initialize the sessionstore service.
   */
  init() {
    if (this._initialized) {
      throw new Error("SessionStore.init() must only be called once!");
    }

    TelemetryTimestamps.add("sessionRestoreInitialized");
    OBSERVING.forEach(function (aTopic) {
      Services.obs.addObserver(this, aTopic, true);
    }, this);

    this._initPrefs();
    this._initialized = true;

    this.promiseAllWindowsRestored.finally(() => () => {
      this._log.debug("promiseAllWindowsRestored finalized");
    });
  },

  /**
   * Initialize the session using the state provided by SessionStartup
   */
  initSession() {
    let timerId = Glean.sessionRestore.startupInitSession.start();
    let state;
    let ss = lazy.SessionStartup;
    let willRestore = ss.willRestore();
    if (willRestore || ss.sessionType == ss.DEFER_SESSION) {
      state = ss.state;
    }
    this._log.debug(
      `initSession willRestore: ${willRestore}, SessionStartup.sessionType: ${ss.sessionType}`
    );

    if (state) {
      try {
        // If we're doing a DEFERRED session, then we want to pull pinned tabs
        // out so they can be restored, and save any open groups so they are
        // available to the user.
        if (ss.sessionType == ss.DEFER_SESSION) {
          let [iniState, remainingState] =
            this._prepDataForDeferredRestore(state);
          // If we have an iniState with windows, that means that we have windows
          // with pinned tabs to restore. If we have an iniState with saved
          // groups, we need to preserve those in the new state.
          if (iniState.windows.length || iniState.savedGroups) {
            state = iniState;
          } else {
            state = null;
          }
          this._log.debug(
            `initSession deferred restore with ${iniState.windows.length} initial windows, ${remainingState.windows.length} remaining windows`
          );

          if (remainingState.windows.length) {
            LastSession.setState(remainingState);
          }
          Glean.browserEngagement.sessionrestoreInterstitial.deferred_restore.add(
            1
          );
        } else {
          // Get the last deferred session in case the user still wants to
          // restore it
          LastSession.setState(state.lastSessionState);

          let restoreAsCrashed = ss.willRestoreAsCrashed();
          if (restoreAsCrashed) {
            this._recentCrashes =
              ((state.session && state.session.recentCrashes) || 0) + 1;
            this._log.debug(
              `initSession, restoreAsCrashed, crashes: ${this._recentCrashes}`
            );

            // _needsRestorePage will record sessionrestore_interstitial,
            // including the specific reason we decided we needed to show
            // about:sessionrestore, if that's what we do.
            if (this._needsRestorePage(state, this._recentCrashes)) {
              // replace the crashed session with a restore-page-only session
              let url = "about:sessionrestore";
              let formdata = { id: { sessionData: state }, url };
              let entry = {
                url,
                triggeringPrincipal_base64:
                  lazy.E10SUtils.SERIALIZED_SYSTEMPRINCIPAL,
              };
              state = { windows: [{ tabs: [{ entries: [entry], formdata }] }] };
              this._log.debug("initSession, will show about:sessionrestore");
            } else if (
              this._hasSingleTabWithURL(state.windows, "about:welcomeback")
            ) {
              this._log.debug("initSession, will show about:welcomeback");
              Glean.browserEngagement.sessionrestoreInterstitial.shown_only_about_welcomeback.add(
                1
              );
              // On a single about:welcomeback URL that crashed, replace about:welcomeback
              // with about:sessionrestore, to make clear to the user that we crashed.
              state.windows[0].tabs[0].entries[0].url = "about:sessionrestore";
              state.windows[0].tabs[0].entries[0].triggeringPrincipal_base64 =
                lazy.E10SUtils.SERIALIZED_SYSTEMPRINCIPAL;
            } else {
              restoreAsCrashed = false;
            }
          }

          // If we didn't use about:sessionrestore, record that:
          if (!restoreAsCrashed) {
            Glean.browserEngagement.sessionrestoreInterstitial.autorestore.add(
              1
            );
            this._log.debug("initSession, will autorestore");
            this._removeExplicitlyClosedTabs(state);
          }

          // Update the session start time using the restored session state.
          this._updateSessionStartTime(state);

          if (state.windows.length) {
            // Make sure that at least the first window doesn't have anything hidden.
            delete state.windows[0].hidden;
            // Since nothing is hidden in the first window, it cannot be a popup.
            delete state.windows[0].isPopup;
            // We don't want to minimize and then open a window at startup.
            if (state.windows[0].sizemode == "minimized") {
              state.windows[0].sizemode = "normal";
            }
          }

          // clear any lastSessionWindowID attributes since those don't matter
          // during normal restore
          state.windows.forEach(function (aWindow) {
            delete aWindow.__lastSessionWindowID;
          });
        }

        // clear _maybeDontRestoreTabs because we have restored (or not)
        // windows and so they don't matter
        state?.windows?.forEach(win => delete win._maybeDontRestoreTabs);
        state?._closedWindows?.forEach(win => delete win._maybeDontRestoreTabs);

        this._savedGroups = state?.savedGroups ?? [];
      } catch (ex) {
        this._log.error("The session file is invalid: ", ex);
      }
    }

    // at this point, we've as good as resumed the session, so we can
    // clear the resume_session_once flag, if it's set
    if (
      !lazy.RunState.isQuitting &&
      this._prefBranch.getBoolPref("sessionstore.resume_session_once")
    ) {
      this._prefBranch.setBoolPref("sessionstore.resume_session_once", false);
    }

    Glean.sessionRestore.startupInitSession.stopAndAccumulate(timerId);
    return state;
  },

  /**
   * When initializing session, if we are restoring the last session at startup,
   * close open tabs or close windows marked _maybeDontRestoreTabs (if they were closed
   * by closing remaining tabs).
   * See bug 490136
   */
  _removeExplicitlyClosedTabs(state) {
    // Don't restore tabs that has been explicitly closed
    for (let i = 0; i < state.windows.length; ) {
      const winData = state.windows[i];
      if (winData._maybeDontRestoreTabs) {
        if (state.windows.length == 1) {
          // it's the last window, we just want to close tabs
          let j = 0;
          // reset close group (we don't want to append tabs to existing group close).
          winData._lastClosedTabGroupCount = -1;
          while (winData.tabs.length) {
            const tabState = winData.tabs.pop();

            // Ensure the index is in bounds.
            let activeIndex = (tabState.index || tabState.entries.length) - 1;
            activeIndex = Math.min(activeIndex, tabState.entries.length - 1);
            activeIndex = Math.max(activeIndex, 0);

            let title = "";
            if (activeIndex in tabState.entries) {
              title =
                tabState.entries[activeIndex].title ||
                tabState.entries[activeIndex].url;
            }

            const tabData = {
              state: tabState,
              title,
              image: tabState.image,
              pos: j++,
              closedAt: Date.now(),
              closedInGroup: true,
            };
            if (this._shouldSaveTabState(tabState)) {
              this.saveClosedTabData(winData, winData._closedTabs, tabData);
            }
          }
        } else {
          // We can remove the window since it doesn't have any
          // tabs that we should restore and it's not the only window
          if (winData.tabs.some(this._shouldSaveTabState)) {
            winData.closedAt = Date.now();
            state._closedWindows.unshift(winData);
          }
          state.windows.splice(i, 1);
          continue; // we don't want to increment the index
        }
      }
      i++;
    }
  },

  _initPrefs() {
    this._prefBranch = Services.prefs.getBranch("browser.");

    gDebuggingEnabled = this._prefBranch.getBoolPref("sessionstore.debug");

    Services.prefs.addObserver("browser.sessionstore.debug", () => {
      gDebuggingEnabled = this._prefBranch.getBoolPref("sessionstore.debug");
    });

    this._log = lazy.sessionStoreLogger;

    this._max_tabs_undo = this._prefBranch.getIntPref(
      "sessionstore.max_tabs_undo"
    );
    this._prefBranch.addObserver("sessionstore.max_tabs_undo", this, true);

    this._closedTabsFromAllWindowsEnabled = this._prefBranch.getBoolPref(
      "sessionstore.closedTabsFromAllWindows"
    );
    this._prefBranch.addObserver(
      "sessionstore.closedTabsFromAllWindows",
      this,
      true
    );

    this._closedTabsFromClosedWindowsEnabled = this._prefBranch.getBoolPref(
      "sessionstore.closedTabsFromClosedWindows"
    );
    this._prefBranch.addObserver(
      "sessionstore.closedTabsFromClosedWindows",
      this,
      true
    );

    this._max_windows_undo = this._prefBranch.getIntPref(
      "sessionstore.max_windows_undo"
    );
    this._prefBranch.addObserver("sessionstore.max_windows_undo", this, true);

    this._restore_on_demand = this._prefBranch.getBoolPref(
      "sessionstore.restore_on_demand"
    );
    this._prefBranch.addObserver("sessionstore.restore_on_demand", this, true);
  },

  /**
   * Called on application shutdown, after notifications:
   * quit-application-granted, quit-application
   */
  _uninit: function ssi_uninit() {
    if (!this._initialized) {
      throw new Error("SessionStore is not initialized.");
    }

    // Prepare to close the session file and write the last state.
    lazy.RunState.setClosing();

    // save all data for session resuming
    if (this._sessionInitialized) {
      lazy.SessionSaver.run();
    }

    // clear out priority queue in case it's still holding refs
    TabRestoreQueue.reset();

    // Make sure to cancel pending saves.
    lazy.SessionSaver.cancel();
  },

  /**
   * Handle notifications
   */
  observe: function ssi_observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "browser-window-before-show": // catch new windows
        this.onBeforeBrowserWindowShown(aSubject);
        break;
      case "domwindowclosed": // catch closed windows
        this.onClose(aSubject).then(() => {
          this._notifyOfClosedObjectsChange();
        });
        if (gDebuggingEnabled) {
          Services.obs.notifyObservers(null, NOTIFY_DOMWINDOWCLOSED_HANDLED);
        }
        break;
      case "quit-application-granted": {
        let syncShutdown = aData == "syncShutdown";
        this.onQuitApplicationGranted(syncShutdown);
        break;
      }
      case "browser-lastwindow-close-granted":
        this.onLastWindowCloseGranted();
        break;
      case "quit-application":
        this.onQuitApplication(aData);
        break;
      case "browser:purge-session-history": // catch sanitization
        this.onPurgeSessionHistory();
        this._notifyOfClosedObjectsChange();
        break;
      case "browser:purge-session-history-for-domain":
        this.onPurgeDomainData(aData);
        this._notifyOfClosedObjectsChange();
        break;
      case "nsPref:changed": // catch pref changes
        this.onPrefChange(aData);
        this._notifyOfClosedObjectsChange();
        break;
      case "idle-daily":
        this.onIdleDaily();
        this._notifyOfClosedObjectsChange();
        break;
      case "clear-origin-attributes-data": {
        let userContextId = 0;
        try {
          userContextId = JSON.parse(aData).userContextId;
        } catch (e) {}
        if (userContextId) {
          this._forgetTabsWithUserContextId(userContextId);
        }
        break;
      }
      case "browsing-context-did-set-embedder":
        if (aSubject === aSubject.top && aSubject.isContent) {
          const permanentKey = aSubject.embedderElement?.permanentKey;
          if (permanentKey) {
            this.maybeRecreateSHistoryListener(permanentKey, aSubject);
          }
        }
        break;
      case "browsing-context-discarded": {
        let permanentKey = aSubject?.embedderElement?.permanentKey;
        if (permanentKey) {
          this._browserSHistoryListener.get(permanentKey)?.unregister();
        }
        break;
      }
      case "browser-shutdown-tabstate-updated":
        this.onFinalTabStateUpdateComplete(aSubject);
        this._notifyOfClosedObjectsChange();
        break;
    }
  },

  getOrCreateSHistoryListener(permanentKey, browsingContext) {
    if (!permanentKey || browsingContext !== browsingContext.top) {
      return null;
    }

    const listener = this._browserSHistoryListener.get(permanentKey);
    if (listener) {
      return listener;
    }

    return this.createSHistoryListener(permanentKey, browsingContext, false);
  },

  maybeRecreateSHistoryListener(permanentKey, browsingContext) {
    const listener = this._browserSHistoryListener.get(permanentKey);
    if (!listener || listener._browserId != browsingContext.browserId) {
      listener?.unregister(permanentKey);
      this.createSHistoryListener(permanentKey, browsingContext, true);
    }
  },

  createSHistoryListener(permanentKey, browsingContext, collectImmediately) {
    class SHistoryListener {
      constructor() {
        this.QueryInterface = ChromeUtils.generateQI([
          "nsISHistoryListener",
          "nsISupportsWeakReference",
        ]);

        this._browserId = browsingContext.browserId;
        this._fromIndex = kNoIndex;
      }

      unregister() {
        let bc = BrowsingContext.getCurrentTopByBrowserId(this._browserId);
        bc?.sessionHistory?.removeSHistoryListener(this);
        SessionStoreInternal._browserSHistoryListener.delete(permanentKey);
      }

      collect(
        permanentKey, // eslint-disable-line no-shadow
        browsingContext, // eslint-disable-line no-shadow
        { collectFull = true, writeToCache = false }
      ) {
        // Don't bother doing anything if we haven't seen any navigations.
        if (!collectFull && this._fromIndex === kNoIndex) {
          return null;
        }

        let timerId = Glean.sessionRestore.collectSessionHistory.start();

        let fromIndex = collectFull ? -1 : this._fromIndex;
        this._fromIndex = kNoIndex;

        let historychange = lazy.SessionHistory.collectFromParent(
          browsingContext.currentURI?.spec,
          true, // Bug 1704574
          browsingContext.sessionHistory,
          fromIndex
        );

        if (writeToCache) {
          let win =
            browsingContext.embedderElement?.ownerGlobal ||
            browsingContext.currentWindowGlobal?.browsingContext?.window;

          SessionStoreInternal.onTabStateUpdate(permanentKey, win, {
            data: { historychange },
          });
        }

        Glean.sessionRestore.collectSessionHistory.stopAndAccumulate(timerId);

        return historychange;
      }

      collectFrom(index) {
        if (this._fromIndex <= index) {
          // If we already know that we need to update history from index N we
          // can ignore any changes that happened with an element with index
          // larger than N.
          //
          // Note: initially we use kNoIndex which is MAX_SAFE_INTEGER which
          // means we don't ignore anything here, and in case of navigation in
          // the history back and forth cases we use kLastIndex which ignores
          // only the subsequent navigations, but not any new elements added.
          return;
        }

        let bc = BrowsingContext.getCurrentTopByBrowserId(this._browserId);
        if (bc?.embedderElement?.frameLoader) {
          this._fromIndex = index;

          // Queue a tab state update on the |browser.sessionstore.interval|
          // timer. We'll call this.collect() when we receive the update.
          bc.embedderElement.frameLoader.requestSHistoryUpdate();
        }
      }

      OnHistoryNewEntry(newURI, oldIndex) {
        // We use oldIndex - 1 to collect the current entry as well. This makes
        // sure to collect any changes that were made to the entry while the
        // document was active.
        this.collectFrom(oldIndex == -1 ? oldIndex : oldIndex - 1);
      }
      OnHistoryGotoIndex() {
        this.collectFrom(kLastIndex);
      }
      OnHistoryPurge() {
        this.collectFrom(-1);
      }
      OnHistoryReload() {
        this.collectFrom(-1);
        return true;
      }
      OnHistoryReplaceEntry() {
        this.collectFrom(-1);
      }
    }

    let sessionHistory = browsingContext.sessionHistory;
    if (!sessionHistory) {
      return null;
    }

    const listener = new SHistoryListener();
    sessionHistory.addSHistoryListener(listener);
    this._browserSHistoryListener.set(permanentKey, listener);

    let isAboutBlank = browsingContext.currentURI?.spec === "about:blank";

    if (collectImmediately && (!isAboutBlank || sessionHistory.count !== 0)) {
      listener.collect(permanentKey, browsingContext, { writeToCache: true });
    }

    return listener;
  },

  onTabStateUpdate(permanentKey, win, update) {
    // Ignore messages from <browser> elements that have crashed
    // and not yet been revived.
    if (this._crashedBrowsers.has(permanentKey)) {
      return;
    }

    lazy.TabState.update(permanentKey, update);
    this.saveStateDelayed(win);

    // Handle any updates sent by the child after the tab was closed. This
    // might be the final update as sent by the "unload" handler but also
    // any async update message that was sent before the child unloaded.
    let closedTab = this._closingTabMap.get(permanentKey);
    if (closedTab) {
      // Update the closed tab's state. This will be reflected in its
      // window's list of closed tabs as that refers to the same object.
      lazy.TabState.copyFromCache(permanentKey, closedTab.tabData.state);
    }
  },

  onFinalTabStateUpdateComplete(browser) {
    let permanentKey = browser.permanentKey;
    if (
      this._closingTabMap.has(permanentKey) &&
      !this._crashedBrowsers.has(permanentKey)
    ) {
      let { winData, closedTabs, tabData } =
        this._closingTabMap.get(permanentKey);

      // We expect no further updates.
      this._closingTabMap.delete(permanentKey);

      // The tab state no longer needs this reference.
      delete tabData.permanentKey;

      // Determine whether the tab state is worth saving.
      let shouldSave = this._shouldSaveTabState(tabData.state);
      let index = closedTabs.indexOf(tabData);

      if (shouldSave && index == -1) {
        // If the tab state is worth saving and we didn't push it onto
        // the list of closed tabs when it was closed (because we deemed
        // the state not worth saving) then add it to the window's list
        // of closed tabs now.
        this.saveClosedTabData(winData, closedTabs, tabData);
      } else if (!shouldSave && index > -1) {
        // Remove from the list of closed tabs. The update messages sent
        // after the tab was closed changed enough state so that we no
        // longer consider its data interesting enough to keep around.
        this.removeClosedTabData(winData, closedTabs, index);
      }

      this._cleanupOrphanedClosedGroups(winData);
    }

    // If this the final message we need to resolve all pending flush
    // requests for the given browser as they might have been sent too
    // late and will never respond. If they have been sent shortly after
    // switching a browser's remoteness there isn't too much data to skip.
    lazy.TabStateFlusher.resolveAll(browser);

    this._browserSHistoryListener.get(permanentKey)?.unregister();
    this._restoreListeners.get(permanentKey)?.unregister();

    Services.obs.notifyObservers(browser, NOTIFY_BROWSER_SHUTDOWN_FLUSH);
  },

  updateSessionStoreFromTablistener(
    browser,
    browsingContext,
    permanentKey,
    update,
    forStorage = false
  ) {
    permanentKey = browser?.permanentKey ?? permanentKey;
    if (!permanentKey) {
      return;
    }

    // Ignore sessionStore update from previous epochs
    if (!this.isCurrentEpoch(permanentKey, update.epoch)) {
      return;
    }

    if (browsingContext.isReplaced) {
      return;
    }

    let listener = this.getOrCreateSHistoryListener(
      permanentKey,
      browsingContext
    );

    if (listener) {
      let historychange =
        // If it is not the scheduled update (tab closed, window closed etc),
        // try to store the loading non-web-controlled page opened in _blank
        // first.
        (forStorage &&
          lazy.SessionHistory.collectNonWebControlledBlankLoadingSession(
            browsingContext
          )) ||
        listener.collect(permanentKey, browsingContext, {
          collectFull: !!update.sHistoryNeeded,
          writeToCache: false,
        });

      if (historychange) {
        update.data.historychange = historychange;
      }
    }

    let win =
      browser?.ownerGlobal ??
      browsingContext.currentWindowGlobal?.browsingContext?.window;

    this.onTabStateUpdate(permanentKey, win, update);
  },

  /* ........ Window Event Handlers .............. */

  /**
   * Implement EventListener for handling various window and tab events
   */
  handleEvent: function ssi_handleEvent(aEvent) {
    let win = aEvent.currentTarget.ownerGlobal;
    let target = aEvent.originalTarget;
    switch (aEvent.type) {
      case "TabOpen":
        this.onTabAdd(win);
        break;
      case "TabBrowserInserted":
        this.onTabBrowserInserted(win, target);
        break;
      case "TabClose":
        // `adoptedBy` will be set if the tab was closed because it is being
        // moved to a new window.
        if (aEvent.detail.adoptedBy) {
          this.onMoveToNewWindow(
            target.linkedBrowser,
            aEvent.detail.adoptedBy.linkedBrowser
          );
        } else if (!aEvent.detail.skipSessionStore) {
          // `skipSessionStore` is set by tab close callers to indicate that we
          // shouldn't record the closed tab.
          this.onTabClose(win, target);
        }
        this.onTabRemove(win, target);
        this._notifyOfClosedObjectsChange();
        break;
      case "TabSelect":
        this.onTabSelect(win);
        break;
      case "TabShow":
        this.onTabShow(win, target);
        break;
      case "TabHide":
        this.onTabHide(win, target);
        break;
      case "TabPinned":
      case "TabUnpinned":
      case "SwapDocShells":
        this.saveStateDelayed(win);
        break;
      case "TabGroupCreate":
      case "TabGroupRemoved":
      case "TabGrouped":
      case "TabUngrouped":
      case "TabGroupCollapse":
      case "TabGroupExpand":
        this.saveStateDelayed(win);
        break;
      case "TabGroupRemoveRequested":
        if (!aEvent.detail?.skipSessionStore) {
          this.onTabGroupRemoveRequested(win, target);
          this._notifyOfClosedObjectsChange();
        }
        break;
      case "oop-browser-crashed":
      case "oop-browser-buildid-mismatch":
        if (aEvent.isTopFrame) {
          this.onBrowserCrashed(target);
        }
        break;
      case "XULFrameLoaderCreated":
        if (
          target.namespaceURI == XUL_NS &&
          target.localName == "browser" &&
          target.frameLoader &&
          target.permanentKey
        ) {
          this._lastKnownFrameLoader.set(
            target.permanentKey,
            target.frameLoader
          );
          this.resetEpoch(target.permanentKey, target.frameLoader);
        }
        break;
      default:
        throw new Error(`unhandled event ${aEvent.type}?`);
    }
    this._clearRestoringWindows();
  },

  /**
   * Generate a unique window identifier
   * @return string
   *         A unique string to identify a window
   */
  _generateWindowID: function ssi_generateWindowID() {
    return "window" + this._nextWindowID++;
  },

  /**
   * Registers and tracks a given window.
   *
   * @param aWindow
   *        Window reference
   */
  onLoad(aWindow) {
    // return if window has already been initialized
    if (aWindow && aWindow.__SSi && this._windows[aWindow.__SSi]) {
      return;
    }

    // ignore windows opened while shutting down
    if (lazy.RunState.isQuitting) {
      return;
    }

    // Assign the window a unique identifier we can use to reference
    // internal data about the window.
    aWindow.__SSi = this._generateWindowID();

    // and create its data object
    this._windows[aWindow.__SSi] = {
      tabs: [],
      groups: [],
      closedGroups: [],
      selected: 0,
      _closedTabs: [],
      // NOTE: this naming refers to the number of tabs in a *multiselection*, not in a tab group.
      // This naming was chosen before the introduction of tab groups proper.
      // TODO: choose more distinct naming in bug1928424
      _lastClosedTabGroupCount: -1,
      lastClosedTabGroupId: null,
      busy: false,
      chromeFlags: aWindow.docShell.treeOwner
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIAppWindow).chromeFlags,
    };

    if (PrivateBrowsingUtils.isWindowPrivate(aWindow)) {
      this._windows[aWindow.__SSi].isPrivate = true;
    }
    if (!this._isWindowLoaded(aWindow)) {
      this._windows[aWindow.__SSi]._restoring = true;
    }
    if (!aWindow.toolbar.visible) {
      this._windows[aWindow.__SSi].isPopup = true;
    }

    if (aWindow.document.documentElement.hasAttribute("taskbartab")) {
      this._windows[aWindow.__SSi].isTaskbarTab = true;
    }

    let tabbrowser = aWindow.gBrowser;

    // add tab change listeners to all already existing tabs
    for (let i = 0; i < tabbrowser.tabs.length; i++) {
      this.onTabBrowserInserted(aWindow, tabbrowser.tabs[i]);
    }
    // notification of tab add/remove/selection/show/hide
    TAB_EVENTS.forEach(function (aEvent) {
      tabbrowser.tabContainer.addEventListener(aEvent, this, true);
    }, this);

    // Keep track of a browser's latest frameLoader.
    aWindow.gBrowser.addEventListener("XULFrameLoaderCreated", this);
  },

  /**
   * Initializes a given window.
   *
   * Windows are registered as soon as they are created but we need to wait for
   * the session file to load, and the initial window's delayed startup to
   * finish before initializing a window, i.e. restoring data into it.
   *
   * @param aWindow
   *        Window reference
   * @param aInitialState
   *        The initial state to be loaded after startup (optional)
   */
  initializeWindow(aWindow, aInitialState = null) {
    let isPrivateWindow = PrivateBrowsingUtils.isWindowPrivate(aWindow);
    let isTaskbarTab = this._windows[aWindow.__SSi].isTaskbarTab;
    // A regular window is not a private window, taskbar tab window, or popup window
    let isRegularWindow =
      !isPrivateWindow && !isTaskbarTab && aWindow.toolbar.visible;

    // perform additional initialization when the first window is loading
    if (lazy.RunState.isStopped) {
      lazy.RunState.setRunning();

      // restore a crashed session resp. resume the last session if requested
      if (aInitialState) {
        // Don't write to disk right after startup. Set the last time we wrote
        // to disk to NOW() to enforce a full interval before the next write.
        lazy.SessionSaver.updateLastSaveTime();

        if (isPrivateWindow || isTaskbarTab) {
          this._log.debug(
            "initializeWindow, the window is private or a web app. Saving SessionStartup.state for possibly restoring later"
          );
          // We're starting with a single private window. Save the state we
          // actually wanted to restore so that we can do it later in case
          // the user opens another, non-private window.
          this._deferredInitialState = lazy.SessionStartup.state;

          // Nothing to restore now, notify observers things are complete.
          Services.obs.notifyObservers(null, NOTIFY_WINDOWS_RESTORED);
          Services.obs.notifyObservers(
            null,
            "sessionstore-one-or-no-tab-restored"
          );
          this._deferredAllWindowsRestored.resolve();
        } else {
          TelemetryTimestamps.add("sessionRestoreRestoring");
          this._restoreCount = aInitialState.windows
            ? aInitialState.windows.length
            : 0;

          // global data must be restored before restoreWindow is called so that
          // it happens before observers are notified
          this._globalState.setFromState(aInitialState);

          // Restore session cookies before loading any tabs.
          lazy.SessionCookies.restore(aInitialState.cookies || []);

          let overwrite = this._isCmdLineEmpty(aWindow, aInitialState);
          let options = { firstWindow: true, overwriteTabs: overwrite };
          this.restoreWindows(aWindow, aInitialState, options);
        }
      } else {
        // Nothing to restore, notify observers things are complete.
        Services.obs.notifyObservers(null, NOTIFY_WINDOWS_RESTORED);
        Services.obs.notifyObservers(
          null,
          "sessionstore-one-or-no-tab-restored"
        );
        this._deferredAllWindowsRestored.resolve();
      }
      // this window was opened by _openWindowWithState
    } else if (!this._isWindowLoaded(aWindow)) {
      // We want to restore windows after all windows have opened (since bug
      // 1034036), so bail out here.
      return;
      // The user opened another window that is not a popup, private window, or web app,
      // after starting up with a single private or web app window.
      // Let's restore the session we actually wanted to restore at startup.
    } else if (this._deferredInitialState && isRegularWindow) {
      // global data must be restored before restoreWindow is called so that
      // it happens before observers are notified
      this._globalState.setFromState(this._deferredInitialState);

      this._restoreCount = this._deferredInitialState.windows
        ? this._deferredInitialState.windows.length
        : 0;
      this.restoreWindows(aWindow, this._deferredInitialState, {
        firstWindow: true,
      });
      this._deferredInitialState = null;
    } else if (
      this._restoreLastWindow &&
      aWindow.toolbar.visible &&
      this._closedWindows.length &&
      !isPrivateWindow
    ) {
      // default to the most-recently closed window
      // don't use popup windows
      let closedWindowState = null;
      let closedWindowIndex;
      for (let i = 0; i < this._closedWindows.length; i++) {
        // Take the first non-popup, point our object at it, and break out.
        if (!this._closedWindows[i].isPopup) {
          closedWindowState = this._closedWindows[i];
          closedWindowIndex = i;
          break;
        }
      }

      if (closedWindowState) {
        let newWindowState;
        if (
          AppConstants.platform == "macosx" ||
          !lazy.SessionStartup.willRestore()
        ) {
          // We want to split the window up into pinned tabs and unpinned tabs.
          // Pinned tabs should be restored. If there are any remaining tabs,
          // they should be added back to _closedWindows.
          // We'll cheat a little bit and reuse _prepDataForDeferredRestore
          // even though it wasn't built exactly for this.
          let [appTabsState, normalTabsState] =
            this._prepDataForDeferredRestore({
              windows: [closedWindowState],
            });

          // These are our pinned tabs and sidebar attributes, which we should restore
          if (appTabsState.windows.length) {
            newWindowState = appTabsState.windows[0];
            delete newWindowState.__lastSessionWindowID;
          }

          // In case there were no unpinned tabs, remove the window from _closedWindows
          if (!normalTabsState.windows.length) {
            this._removeClosedWindow(closedWindowIndex);
            // Or update _closedWindows with the modified state
          } else {
            delete normalTabsState.windows[0].__lastSessionWindowID;
            this._closedWindows[closedWindowIndex] = normalTabsState.windows[0];
          }
        } else {
          // If we're just restoring the window, make sure it gets removed from
          // _closedWindows.
          this._removeClosedWindow(closedWindowIndex);
          newWindowState = closedWindowState;
          delete newWindowState.hidden;
        }

        if (newWindowState) {
          // Ensure that the window state isn't hidden
          this._restoreCount = 1;
          let state = { windows: [newWindowState] };
          let options = { overwriteTabs: this._isCmdLineEmpty(aWindow, state) };
          this.restoreWindow(aWindow, newWindowState, options);
        }
      }
      // we actually restored the session just now.
      this._prefBranch.setBoolPref("sessionstore.resume_session_once", false);
    }
    // This is a taskbar-tab specific scenario. If an user closes
    // all regular Firefox windows except for taskbar tabs and has
    // auto restore on startup enabled, _shouldRestoreLastSession
    // will be set to true. We should then restore when a
    // regular Firefox window is opened.
    else if (
      Services.prefs.getBoolPref("browser.taskbarTabs.enabled", false) &&
      this._shouldRestoreLastSession &&
      isRegularWindow
    ) {
      let lastSessionState = LastSession.getState();
      this._globalState.setFromState(lastSessionState);
      lazy.SessionCookies.restore(lastSessionState.cookies || []);
      this.restoreWindows(aWindow, lastSessionState, {
        firstWindow: true,
      });
      this._shouldRestoreLastSession = false;
    }

    if (this._restoreLastWindow && aWindow.toolbar.visible) {
      // always reset (if not a popup window)
      // we don't want to restore a window directly after, for example,
      // undoCloseWindow was executed.
      this._restoreLastWindow = false;
    }
  },

  /**
   * Called right before a new browser window is shown.
   * @param aWindow
   *        Window reference
   */
  onBeforeBrowserWindowShown(aWindow) {
    // Register the window.
    this.onLoad(aWindow);

    // Some are waiting for this window to be shown, which is now, so let's resolve
    // the deferred operation.
    let deferred = WINDOW_SHOWING_PROMISES.get(aWindow);
    if (deferred) {
      deferred.resolve(aWindow);
      WINDOW_SHOWING_PROMISES.delete(aWindow);
    }

    // Just call initializeWindow() directly if we're initialized already.
    if (this._sessionInitialized) {
      this._log.debug(
        "onBeforeBrowserWindowShown, session already initialized, initializing window"
      );
      this.initializeWindow(aWindow);
      return;
    }

    // The very first window that is opened creates a promise that is then
    // re-used by all subsequent windows. The promise will be used to tell
    // when we're ready for initialization.
    if (!this._promiseReadyForInitialization) {
      // Wait for the given window's delayed startup to be finished.
      let promise = new Promise(resolve => {
        Services.obs.addObserver(function obs(subject, topic) {
          if (aWindow == subject) {
            Services.obs.removeObserver(obs, topic);
            resolve();
          }
        }, "browser-delayed-startup-finished");
      });

      // We are ready for initialization as soon as the session file has been
      // read from disk and the initial window's delayed startup has finished.
      this._promiseReadyForInitialization = Promise.all([
        promise,
        lazy.SessionStartup.onceInitialized,
      ]);
    }

    // We can't call this.onLoad since initialization
    // hasn't completed, so we'll wait until it is done.
    // Even if additional windows are opened and wait
    // for initialization as well, the first opened
    // window should execute first, and this.onLoad
    // will be called with the initialState.
    this._promiseReadyForInitialization
      .then(() => {
        if (aWindow.closed) {
          this._log.debug(
            "When _promiseReadyForInitialization resolved, the window was closed"
          );
          return;
        }

        if (this._sessionInitialized) {
          this.initializeWindow(aWindow);
        } else {
          let initialState = this.initSession();
          this._sessionInitialized = true;

          if (initialState) {
            Services.obs.notifyObservers(null, NOTIFY_RESTORING_ON_STARTUP);
          }
          let timerId = Glean.sessionRestore.startupOnloadInitialWindow.start();
          this.initializeWindow(aWindow, initialState);
          Glean.sessionRestore.startupOnloadInitialWindow.stopAndAccumulate(
            timerId
          );

          // Let everyone know we're done.
          this._deferredInitialized.resolve();
        }
      })
      .catch(ex => {
        this._log.error(
          "Exception when handling _promiseReadyForInitialization resolution:",
          ex
        );
      });
  },

  /**
   * On window close...
   * - remove event listeners from tabs
   * - save all window data
   * @param aWindow
   *        Window reference
   *
   * @returns a Promise
   */
  onClose: function ssi_onClose(aWindow) {
    let completionPromise = Promise.resolve();
    // this window was about to be restored - conserve its original data, if any
    let isFullyLoaded = this._isWindowLoaded(aWindow);
    if (!isFullyLoaded) {
      if (!aWindow.__SSi) {
        aWindow.__SSi = this._generateWindowID();
      }

      let restoreID = WINDOW_RESTORE_IDS.get(aWindow);
      this._windows[aWindow.__SSi] =
        this._statesToRestore[restoreID].windows[0];
      delete this._statesToRestore[restoreID];
      WINDOW_RESTORE_IDS.delete(aWindow);
    }

    // ignore windows not tracked by SessionStore
    if (!aWindow.__SSi || !this._windows[aWindow.__SSi]) {
      return completionPromise;
    }

    // notify that the session store will stop tracking this window so that
    // extensions can store any data about this window in session store before
    // that's not possible anymore
    let event = aWindow.document.createEvent("Events");
    event.initEvent("SSWindowClosing", true, false);
    aWindow.dispatchEvent(event);

    if (this.windowToFocus && this.windowToFocus == aWindow) {
      delete this.windowToFocus;
    }

    var tabbrowser = aWindow.gBrowser;

    let browsers = Array.from(tabbrowser.browsers);

    TAB_EVENTS.forEach(function (aEvent) {
      tabbrowser.tabContainer.removeEventListener(aEvent, this, true);
    }, this);

    aWindow.gBrowser.removeEventListener("XULFrameLoaderCreated", this);

    let winData = this._windows[aWindow.__SSi];

    // Collect window data only when *not* closed during shutdown.
    if (lazy.RunState.isRunning) {
      // Grab the most recent window data. The tab data will be updated
      // once we finish flushing all of the messages from the tabs.
      let tabMap = this._collectWindowData(aWindow);

      for (let [tab, tabData] of tabMap) {
        let permanentKey = tab.linkedBrowser.permanentKey;
        this._tabClosingByWindowMap.set(permanentKey, tabData);
      }

      if (isFullyLoaded && !winData.title) {
        winData.title =
          tabbrowser.selectedBrowser.contentTitle ||
          tabbrowser.selectedTab.label;
      }

      if (AppConstants.platform != "macosx") {
        // Until we decide otherwise elsewhere, this window is part of a series
        // of closing windows to quit.
        winData._shouldRestore = true;
      }

      // Store the window's close date to figure out when each individual tab
      // was closed. This timestamp should allow re-arranging data based on how
      // recently something was closed.
      winData.closedAt = Date.now();

      // we don't want to save the busy state
      delete winData.busy;

      // When closing windows one after the other until Firefox quits, we
      // will move those closed in series back to the "open windows" bucket
      // before writing to disk. If however there is only a single window
      // with tabs we deem not worth saving then we might end up with a
      // random closed or even a pop-up window re-opened. To prevent that
      // we explicitly allow saving an "empty" window state.
      let isLastWindow = this.isLastRestorableWindow();

      let isLastRegularWindow =
        Object.values(this._windows).filter(
          wData => !wData.isPrivate && !wData.isTaskbarTab
        ).length == 1;

      let taskbarTabsRemains = Object.values(this._windows).some(
        wData => wData.isTaskbarTab
      );

      // Closing the last regular Firefox window with
      // at least one taskbar tab window still active.
      // The session is considered over and we need to restore
      // the next time a non-private, non-taskbar-tab window
      // is opened.
      if (
        Services.prefs.getBoolPref("browser.taskbarTabs.enabled", false) &&
        isLastRegularWindow &&
        !winData.isTaskbarTab &&
        !winData.isPrivate &&
        taskbarTabsRemains
      ) {
        // If the setting is enabled, Firefox should auto-restore
        // the next time a regular window is opened
        if (this.willAutoRestore) {
          this._shouldRestoreLastSession = true;
          // Otherwise, we want "restore last session" button
          // to be avaliable in the hamburger menu
        } else {
          Services.obs.notifyObservers(null, NOTIFY_LAST_SESSION_RE_ENABLED);
        }

        let savedState = this.getCurrentState(true);
        lazy.PrivacyFilter.filterPrivateWindowsAndTabs(savedState);
        LastSession.setState(savedState);
        this._restoreWithoutRestart = true;
      }

      // clear this window from the list, since it has definitely been closed.
      delete this._windows[aWindow.__SSi];

      // This window has the potential to be saved in the _closedWindows
      // array (maybeSaveClosedWindows gets the final call on that).
      this._saveableClosedWindowData.add(winData);

      // Now we have to figure out if this window is worth saving in the _closedWindows
      // Object.
      //
      // We're about to flush the tabs from this window, but it's possible that we
      // might never hear back from the content process(es) in time before the user
      // chooses to restore the closed window. So we do the following:
      //
      // 1) Use the tab state cache to determine synchronously if the window is
      //    worth stashing in _closedWindows.
      // 2) Flush the window.
      // 3) When the flush is complete, revisit our decision to store the window
      //    in _closedWindows, and add/remove as necessary.
      if (!winData.isPrivate && !winData.isTaskbarTab) {
        this.maybeSaveClosedWindow(winData, isLastWindow);
      }

      completionPromise = lazy.TabStateFlusher.flushWindow(aWindow).then(() => {
        // At this point, aWindow is closed! You should probably not try to
        // access any DOM elements from aWindow within this callback unless
        // you're holding on to them in the closure.

        WINDOW_FLUSHING_PROMISES.delete(aWindow);

        for (let browser of browsers) {
          if (this._tabClosingByWindowMap.has(browser.permanentKey)) {
            let tabData = this._tabClosingByWindowMap.get(browser.permanentKey);
            lazy.TabState.copyFromCache(browser.permanentKey, tabData);
            this._tabClosingByWindowMap.delete(browser.permanentKey);
          }
        }

        // Save non-private windows if they have at
        // least one saveable tab or are the last window.
        if (!winData.isPrivate && !winData.isTaskbarTab) {
          this.maybeSaveClosedWindow(winData, isLastWindow);

          if (!isLastWindow && winData.closedId > -1) {
            this._addClosedAction(
              this._LAST_ACTION_CLOSED_WINDOW,
              winData.closedId
            );
          }
        }

        // Update the tabs data now that we've got the most
        // recent information.
        this.cleanUpWindow(aWindow, winData, browsers);

        // save the state without this window to disk
        this.saveStateDelayed();
      });

      // Here we might override a flush already in flight, but that's fine
      // because `completionPromise` will always resolve after the old flush
      // resolves.
      WINDOW_FLUSHING_PROMISES.set(aWindow, completionPromise);
    } else {
      this.cleanUpWindow(aWindow, winData, browsers);
    }

    for (let i = 0; i < tabbrowser.tabs.length; i++) {
      this.onTabRemove(aWindow, tabbrowser.tabs[i], true);
    }

    return completionPromise;
  },

  /**
   * Clean up the message listeners on a window that has finally
   * gone away. Call this once you're sure you don't want to hear
   * from any of this windows tabs from here forward.
   *
   * @param aWindow
   *        The browser window we're cleaning up.
   * @param winData
   *        The data for the window that we should hold in the
   *        DyingWindowCache in case anybody is still holding a
   *        reference to it.
   */
  cleanUpWindow(aWindow, winData, browsers) {
    // Any leftover TabStateFlusher Promises need to be resolved now,
    // since we're about to remove the message listeners.
    for (let browser of browsers) {
      lazy.TabStateFlusher.resolveAll(browser);
    }

    // Cache the window state until it is completely gone.
    DyingWindowCache.set(aWindow, winData);

    this._saveableClosedWindowData.delete(winData);
    delete aWindow.__SSi;
  },

  /**
   * Decides whether or not a closed window should be put into the
   * _closedWindows Object. This might be called multiple times per
   * window, and will do the right thing of moving the window data
   * in or out of _closedWindows if the winData indicates that our
   * need for saving it has changed.
   *
   * @param winData
   *        The data for the closed window that we might save.
   * @param isLastWindow
   *        Whether or not the window being closed is the last
   *        browser window. Callers of this function should pass
   *        in the value of SessionStoreInternal.atLastWindow for
   *        this argument, and pass in the same value if they happen
   *        to call this method again asynchronously (for example, after
   *        a window flush).
   */
  maybeSaveClosedWindow(winData, isLastWindow) {
    // Make sure SessionStore is still running, and make sure that we
    // haven't chosen to forget this window.
    if (
      lazy.RunState.isRunning &&
      this._saveableClosedWindowData.has(winData)
    ) {
      // Determine whether the window has any tabs worth saving.
      // Note: We currently ignore the possibility of useful _closedTabs here.
      // A window with 0 worth-keeping open tabs will not have its state saved, and
      // any _closedTabs will be lost.
      let hasSaveableTabs = winData.tabs.some(this._shouldSaveTabState);

      // Note that we might already have this window stored in
      // _closedWindows from a previous call to this function.
      let winIndex = this._closedWindows.indexOf(winData);
      let alreadyStored = winIndex != -1;
      // If sidebar command is truthy, i.e. sidebar is open, store sidebar settings
      let shouldStore = hasSaveableTabs || isLastWindow;

      if (shouldStore && !alreadyStored) {
        let index = this._closedWindows.findIndex(win => {
          return win.closedAt < winData.closedAt;
        });

        // If we found no window closed before our
        // window then just append it to the list.
        if (index == -1) {
          index = this._closedWindows.length;
        }

        // About to save the closed window, add a unique ID.
        winData.closedId = this._nextClosedId++;

        // Insert winData at the right position.
        this._closedWindows.splice(index, 0, winData);
        this._capClosedWindows();
        this._saveOpenTabGroupsOnClose(winData);
        this._closedObjectsChanged = true;
        // The first time we close a window, ensure it can be restored from the
        // hidden window.
        if (
          AppConstants.platform == "macosx" &&
          this._closedWindows.length == 1
        ) {
          // Fake a popupshowing event so shortcuts work:
          let window = Services.appShell.hiddenDOMWindow;
          let historyMenu = window.document.getElementById("history-menu");
          let evt = new window.CustomEvent("popupshowing", { bubbles: true });
          historyMenu.menupopup.dispatchEvent(evt);
        }
      } else if (!shouldStore) {
        if (
          winData._closedTabs.length &&
          this._closedTabsFromAllWindowsEnabled
        ) {
          // we are going to lose closed tabs, so any observers should be notified
          this._closedObjectsChanged = true;
        }
        if (alreadyStored) {
          this._removeClosedWindow(winIndex);
          return;
        }
        this._log.warn(
          `Discarding window with 0 saveable tabs and ${winData._closedTabs.length} closed tabs`
        );
      }
    }
  },

  /**
   * If there are any open tab groups in this closing window, move those
   * tab groups to the list of saved tab groups so that the user doesn't
   * lose them.
   *
   * The normal API for saving a tab group is `this.addSavedTabGroup`.
   * `this.addSavedTabGroup` relies on a MozTabbrowserTabGroup DOM element
   * and relies on passing the tab group's MozTabbrowserTab DOM elements to
   * `this.maybeSaveClosedTab`. Since this method might be dealing with a closed
   * window that has no DOM, this method has a separate but similar
   * implementation to `this.addSavedTabGroup` and `this.maybeSaveClosedTab`.
   *
   * @param {WindowStateData} closedWinData
   * @returns {void}
   */
  _saveOpenTabGroupsOnClose(closedWinData) {
    /** @type Map<string, SavedTabGroupStateData> */
    let newlySavedTabGroups = new Map();
    // Convert any open tab groups into saved tab groups in place
    closedWinData.groups = closedWinData.groups.map(tabGroupState =>
      lazy.TabGroupState.savedInClosedWindow(
        tabGroupState,
        closedWinData.closedId
      )
    );
    for (let tabGroupState of closedWinData.groups) {
      if (!tabGroupState.saveOnWindowClose) {
        continue;
      }
      newlySavedTabGroups.set(tabGroupState.id, tabGroupState);
    }
    for (let tIndex = 0; tIndex < closedWinData.tabs.length; tIndex++) {
      let tabState = closedWinData.tabs[tIndex];
      if (!tabState.groupId) {
        continue;
      }
      if (!newlySavedTabGroups.has(tabState.groupId)) {
        continue;
      }

      if (this._shouldSaveTabState(tabState)) {
        let tabData = this._formatTabStateForSavedGroup(tabState);
        if (!tabData) {
          continue;
        }
        newlySavedTabGroups.get(tabState.groupId).tabs.push(tabData);
      }
    }

    // Add saved tab group references to saved tab group state.
    for (let tabGroupToSave of newlySavedTabGroups.values()) {
      this._recordSavedTabGroupState(tabGroupToSave);
    }
  },

  /**
   * Convert tab state into a saved group tab state. Used to convert a
   * closed tab group into a saved tab group.
   *
   * @param {TabState} tabState closed tab state
   */
  _formatTabStateForSavedGroup(tabState) {
    // Ensure the index is in bounds.
    let activeIndex = tabState.index;
    activeIndex = Math.min(activeIndex, tabState.entries.length - 1);
    activeIndex = Math.max(activeIndex, 0);
    if (!(activeIndex in tabState.entries)) {
      return {};
    }
    let title =
      tabState.entries[activeIndex].title || tabState.entries[activeIndex].url;
    return {
      state: tabState,
      title,
      image: tabState.image,
      pos: tabState.pos,
      closedAt: Date.now(),
      closedId: this._nextClosedId++,
    };
  },

  /**
   * On quit application granted
   */
  onQuitApplicationGranted: function ssi_onQuitApplicationGranted(
    syncShutdown = false
  ) {
    // Collect an initial snapshot of window data before we do the flush.
    let index = 0;
    for (let window of this._orderedBrowserWindows) {
      this._collectWindowData(window);
      this._windows[window.__SSi].zIndex = ++index;
    }

    // Now add an AsyncShutdown blocker that'll spin the event loop
    // until the windows have all been flushed.

    // This progress object will track the state of async window flushing
    // and will help us debug things that go wrong with our AsyncShutdown
    // blocker.
    let progress = { total: -1, current: -1 };

    // We're going down! Switch state so that we treat closing windows and
    // tabs correctly.
    lazy.RunState.setQuitting();

    if (!syncShutdown) {
      // We've got some time to shut down, so let's do this properly that there
      // will be a complete session available upon next startup.
      // We use our own timer and spin the event loop ourselves, as we do not
      // want to crash on timeout and as we need to run in response to
      // "quit-application-granted", which is not yet a real shutdown phase.
      //
      // We end spinning once:
      // 1. the flush duration exceeds 10 seconds before DELAY_CRASH_MS, or
      // 2. 'oop-frameloader-crashed' (issued by BrowserParent::ActorDestroy
      //    on abnormal frame shutdown) is observed, or
      // 3. 'ipc:content-shutdown' (issued by ContentParent::ActorDestroy on
      //    abnormal shutdown) is observed, or
      // 4. flushAllWindowsAsync completes (hopefully the normal case).

      // Set up the list of promises that will signal a complete sessionstore
      // shutdown: either all data is saved, or we crashed or the message IPC
      // channel went away in the meantime.
      let promises = [this.flushAllWindowsAsync(progress)];

      const observeTopic = topic => {
        let deferred = Promise.withResolvers();
        const observer = subject => {
          // Skip abort on ipc:content-shutdown if not abnormal/crashed
          subject.QueryInterface(Ci.nsIPropertyBag2);
          if (!(topic == "ipc:content-shutdown" && !subject.get("abnormal"))) {
            deferred.resolve();
          }
        };
        const cleanup = () => {
          try {
            Services.obs.removeObserver(observer, topic);
          } catch (ex) {
            console.error(
              "SessionStore: exception whilst flushing all windows: ",
              ex
            );
          }
        };
        Services.obs.addObserver(observer, topic);
        deferred.promise.then(cleanup, cleanup);
        return deferred;
      };

      // Build a list of deferred executions that require cleanup once the
      // Promise race is won.
      // Ensure that the timer fires earlier than the AsyncShutdown crash timer.
      let waitTimeMaxMs = Math.max(
        0,
        lazy.AsyncShutdown.DELAY_CRASH_MS - 10000
      );
      let defers = [
        this.looseTimer(waitTimeMaxMs),

        // FIXME: We should not be aborting *all* flushes when a single
        // content process crashes here.
        observeTopic("oop-frameloader-crashed"),
        observeTopic("ipc:content-shutdown"),
      ];
      // Add these monitors to the list of Promises to start the race.
      promises.push(...defers.map(deferred => deferred.promise));

      let isDone = false;
      Promise.race(promises)
        .then(() => {
          // When a Promise won the race, make sure we clean up the running
          // monitors.
          defers.forEach(deferred => deferred.reject());
        })
        .finally(() => {
          isDone = true;
        });
      Services.tm.spinEventLoopUntil(
        "Wait until SessionStoreInternal.flushAllWindowsAsync finishes.",
        () => isDone
      );
    } else {
      // We have to shut down NOW, which means we only get to save whatever
      // we already had cached.
    }
  },

  /**
   * An async Task that iterates all open browser windows and flushes
   * any outstanding messages from their tabs. This will also close
   * all of the currently open windows while we wait for the flushes
   * to complete.
   *
   * @param progress (Object)
   *        Optional progress object that will be updated as async
   *        window flushing progresses. flushAllWindowsSync will
   *        write to the following properties:
   *
   *        total (int):
   *          The total number of windows to be flushed.
   *        current (int):
   *          The current window that we're waiting for a flush on.
   *
   * @return Promise
   */
  async flushAllWindowsAsync(progress = {}) {
    let windowPromises = new Map(WINDOW_FLUSHING_PROMISES);
    WINDOW_FLUSHING_PROMISES.clear();

    // We collect flush promises and close each window immediately so that
    // the user can't start changing any window state while we're waiting
    // for the flushes to finish.
    for (let window of this._browserWindows) {
      windowPromises.set(window, lazy.TabStateFlusher.flushWindow(window));

      // We have to wait for these messages to come up from
      // each window and each browser. In the meantime, hide
      // the windows to improve perceived shutdown speed.
      let baseWin = window.docShell.treeOwner.QueryInterface(Ci.nsIBaseWindow);
      baseWin.visibility = false;
    }

    progress.total = windowPromises.size;
    progress.current = 0;

    // We'll iterate through the Promise array, yielding each one, so as to
    // provide useful progress information to AsyncShutdown.
    for (let [win, promise] of windowPromises) {
      await promise;

      // We may have already stopped tracking this window in onClose, which is
      // fine as we would've collected window data there as well.
      if (win.__SSi && this._windows[win.__SSi]) {
        this._collectWindowData(win);
      }

      progress.current++;
    }

    // We must cache this because _getTopWindow will always
    // return null by the time quit-application occurs.
    var activeWindow = this._getTopWindow();
    if (activeWindow) {
      this.activeWindowSSiCache = activeWindow.__SSi || "";
    }
    DirtyWindows.clear();
  },

  /**
   * On last browser window close
   */
  onLastWindowCloseGranted: function ssi_onLastWindowCloseGranted() {
    // last browser window is quitting.
    // remember to restore the last window when another browser window is opened
    // do not account for pref(resume_session_once) at this point, as it might be
    // set by another observer getting this notice after us
    this._restoreLastWindow = true;
  },

  /**
   * On quitting application
   * @param aData
   *        String type of quitting
   */
  onQuitApplication: function ssi_onQuitApplication(aData) {
    if (aData == "restart" || aData == "os-restart") {
      if (!PrivateBrowsingUtils.permanentPrivateBrowsing) {
        if (
          aData == "os-restart" &&
          !this._prefBranch.getBoolPref("sessionstore.resume_session_once")
        ) {
          this._prefBranch.setBoolPref(
            "sessionstore.resuming_after_os_restart",
            true
          );
        }
        this._prefBranch.setBoolPref("sessionstore.resume_session_once", true);
      }

      // The browser:purge-session-history notification fires after the
      // quit-application notification so unregister the
      // browser:purge-session-history notification to prevent clearing
      // session data on disk on a restart.  It is also unnecessary to
      // perform any other sanitization processing on a restart as the
      // browser is about to exit anyway.
      Services.obs.removeObserver(this, "browser:purge-session-history");
    }

    if (aData != "restart") {
      // Throw away the previous session on shutdown without notification
      LastSession.clear(true);
    }

    this._uninit();
  },

  /**
   * Clear session store data for a given private browsing window.
   * @param {ChromeWindow} win - Open private browsing window to clear data for.
   */
  purgeDataForPrivateWindow(win) {
    // No need to clear data if already shutting down.
    if (lazy.RunState.isQuitting) {
      return;
    }

    // Check if we have data for the given window.
    let windowData = this._windows[win.__SSi];
    if (!windowData) {
      return;
    }

    // Clear closed tab data.
    if (windowData._closedTabs.length) {
      // Remove all of the closed tabs data.
      // This also clears out the permenentKey-mapped data for pending state updates
      // and removes the tabs from from the _lastClosedActions list
      while (windowData._closedTabs.length) {
        this.removeClosedTabData(windowData, windowData._closedTabs, 0);
      }
      // Reset the closed tab list.
      windowData._closedTabs = [];
      windowData._lastClosedTabGroupCount = -1;
      windowData.lastClosedTabGroupId = null;
      this._closedObjectsChanged = true;
    }

    // Clear closed tab groups
    if (windowData.closedGroups.length) {
      for (let closedGroup of windowData.closedGroups) {
        while (closedGroup.tabs.length) {
          this.removeClosedTabData(windowData, closedGroup.tabs, 0);
        }
      }
      windowData.closedGroups = [];
      this._closedObjectsChanged = true;
    }
  },

  /**
   * On purge of session history
   */
  onPurgeSessionHistory: function ssi_onPurgeSessionHistory() {
    lazy.SessionFile.wipe();
    // If the browser is shutting down, simply return after clearing the
    // session data on disk as this notification fires after the
    // quit-application notification so the browser is about to exit.
    if (lazy.RunState.isQuitting) {
      return;
    }
    LastSession.clear();

    let openWindows = {};
    // Collect open windows.
    for (let window of this._browserWindows) {
      openWindows[window.__SSi] = true;
    }

    // also clear all data about closed tabs and windows
    for (let ix in this._windows) {
      if (ix in openWindows) {
        if (this._windows[ix]._closedTabs.length) {
          this._windows[ix]._closedTabs = [];
          this._closedObjectsChanged = true;
        }
        if (this._windows[ix].closedGroups.length) {
          this._windows[ix].closedGroups = [];
          this._closedObjectsChanged = true;
        }
      } else {
        delete this._windows[ix];
      }
    }
    // also clear all data about closed windows
    if (this._closedWindows.length) {
      this._closedWindows = [];
      this._closedObjectsChanged = true;
    }
    // give the tabbrowsers a chance to clear their histories first
    var win = this._getTopWindow();
    if (win) {
      win.setTimeout(() => lazy.SessionSaver.run(), 0);
    } else if (lazy.RunState.isRunning) {
      lazy.SessionSaver.run();
    }

    this._clearRestoringWindows();
    this._saveableClosedWindowData = new WeakSet();
    this._lastClosedActions = [];
  },

  /**
   * On purge of domain data
   * @param {string} aDomain
   *        The domain we want to purge data for
   */
  onPurgeDomainData: function ssi_onPurgeDomainData(aDomain) {
    // does a session history entry contain a url for the given domain?
    function containsDomain(aEntry) {
      let host;
      try {
        host = Services.io.newURI(aEntry.url).host;
      } catch (e) {
        // The given URL probably doesn't have a host.
      }
      if (host && Services.eTLD.hasRootDomain(host, aDomain)) {
        return true;
      }
      return aEntry.children && aEntry.children.some(containsDomain, this);
    }
    // remove all closed tabs containing a reference to the given domain
    for (let ix in this._windows) {
      let closedTabsLists = [
        this._windows[ix]._closedTabs,
        ...this._windows[ix].closedGroups.map(g => g.tabs),
      ];

      for (let closedTabs of closedTabsLists) {
        for (let i = closedTabs.length - 1; i >= 0; i--) {
          if (closedTabs[i].state.entries.some(containsDomain, this)) {
            closedTabs.splice(i, 1);
            this._closedObjectsChanged = true;
          }
        }
      }
    }
    // remove all open & closed tabs containing a reference to the given
    // domain in closed windows
    for (let ix = this._closedWindows.length - 1; ix >= 0; ix--) {
      let closedTabsLists = [
        this._closedWindows[ix]._closedTabs,
        ...this._closedWindows[ix].closedGroups.map(g => g.tabs),
      ];
      let openTabs = this._closedWindows[ix].tabs;
      let openTabCount = openTabs.length;

      for (let closedTabs of closedTabsLists) {
        for (let i = closedTabs.length - 1; i >= 0; i--) {
          if (closedTabs[i].state.entries.some(containsDomain, this)) {
            closedTabs.splice(i, 1);
          }
        }
      }
      for (let j = openTabs.length - 1; j >= 0; j--) {
        if (openTabs[j].entries.some(containsDomain, this)) {
          openTabs.splice(j, 1);
          if (this._closedWindows[ix].selected > j) {
            this._closedWindows[ix].selected--;
          }
        }
      }
      if (!openTabs.length) {
        this._closedWindows.splice(ix, 1);
      } else if (openTabs.length != openTabCount) {
        // Adjust the window's title if we removed an open tab
        let selectedTab = openTabs[this._closedWindows[ix].selected - 1];
        // some duplication from restoreHistory - make sure we get the correct title
        let activeIndex = (selectedTab.index || selectedTab.entries.length) - 1;
        if (activeIndex >= selectedTab.entries.length) {
          activeIndex = selectedTab.entries.length - 1;
        }
        this._closedWindows[ix].title = selectedTab.entries[activeIndex].title;
      }
    }

    if (lazy.RunState.isRunning) {
      lazy.SessionSaver.run();
    }

    this._clearRestoringWindows();
  },

  /**
   * On preference change
   * @param aData
   *        String preference changed
   */
  onPrefChange: function ssi_onPrefChange(aData) {
    switch (aData) {
      // if the user decreases the max number of closed tabs they want
      // preserved update our internal states to match that max
      case "sessionstore.max_tabs_undo":
        this._max_tabs_undo = this._prefBranch.getIntPref(
          "sessionstore.max_tabs_undo"
        );
        for (let ix in this._windows) {
          if (this._windows[ix]._closedTabs.length > this._max_tabs_undo) {
            this._windows[ix]._closedTabs.splice(
              this._max_tabs_undo,
              this._windows[ix]._closedTabs.length
            );
            this._closedObjectsChanged = true;
          }
        }
        break;
      case "sessionstore.max_windows_undo":
        this._max_windows_undo = this._prefBranch.getIntPref(
          "sessionstore.max_windows_undo"
        );
        this._capClosedWindows();
        break;
      case "sessionstore.restore_on_demand":
        this._restore_on_demand = this._prefBranch.getBoolPref(
          "sessionstore.restore_on_demand"
        );
        break;
      case "sessionstore.closedTabsFromAllWindows":
        this._closedTabsFromAllWindowsEnabled = this._prefBranch.getBoolPref(
          "sessionstore.closedTabsFromAllWindows"
        );
        this._closedObjectsChanged = true;
        break;
      case "sessionstore.closedTabsFromClosedWindows":
        this._closedTabsFromClosedWindowsEnabled = this._prefBranch.getBoolPref(
          "sessionstore.closedTabsFromClosedWindows"
        );
        this._closedObjectsChanged = true;
        break;
    }
  },

  /**
   * save state when new tab is added
   * @param aWindow
   *        Window reference
   */
  onTabAdd: function ssi_onTabAdd(aWindow) {
    this.saveStateDelayed(aWindow);
  },

  /**
   * set up listeners for a new tab
   * @param aWindow
   *        Window reference
   * @param aTab
   *        Tab reference
   */
  onTabBrowserInserted: function ssi_onTabBrowserInserted(aWindow, aTab) {
    let browser = aTab.linkedBrowser;
    browser.addEventListener("SwapDocShells", this);
    browser.addEventListener("oop-browser-crashed", this);
    browser.addEventListener("oop-browser-buildid-mismatch", this);

    if (browser.frameLoader) {
      this._lastKnownFrameLoader.set(browser.permanentKey, browser.frameLoader);
    }

    // Only restore if browser has been lazy.
    if (
      TAB_LAZY_STATES.has(aTab) &&
      !TAB_STATE_FOR_BROWSER.has(browser) &&
      lazy.TabStateCache.get(browser.permanentKey)
    ) {
      let tabState = lazy.TabState.clone(aTab, TAB_CUSTOM_VALUES.get(aTab));
      this.restoreTab(aTab, tabState);
    }

    // The browser has been inserted now, so lazy data is no longer relevant.
    TAB_LAZY_STATES.delete(aTab);
  },

  /**
   * remove listeners for a tab
   * @param aWindow
   *        Window reference
   * @param aTab
   *        Tab reference
   * @param aNoNotification
   *        bool Do not save state if we're updating an existing tab
   */
  onTabRemove: function ssi_onTabRemove(aWindow, aTab, aNoNotification) {
    this.cleanUpRemovedBrowser(aTab);

    if (!aNoNotification) {
      this.saveStateDelayed(aWindow);
    }
  },

  /**
   * When a tab closes, collect its properties
   * @param {Window} aWindow
   *        Window reference
   * @param {MozTabbrowserTab} aTab
   *        Tab reference
   */
  onTabClose: function ssi_onTabClose(aWindow, aTab) {
    // don't update our internal state if we don't have to
    if (this._max_tabs_undo == 0) {
      return;
    }

    // Get the latest data for this tab (generally, from the cache)
    let tabState = lazy.TabState.collect(aTab, TAB_CUSTOM_VALUES.get(aTab));

    // Store closed-tab data for undo.
    this.maybeSaveClosedTab(aWindow, aTab, tabState);
  },

  onTabGroupRemoveRequested: function ssi_onTabGroupRemoveRequested(
    win,
    tabGroup
  ) {
    // don't update our internal state if we don't have to
    if (this._max_tabs_undo == 0) {
      return;
    }

    if (this.getSavedTabGroup(tabGroup.id)) {
      // If a tab group is being removed from the tab strip but it's already
      // saved, then this is a "save and close" action; the saved tab group
      // should be stored in global session state rather than in this window's
      // closed tab groups.
      return;
    }

    let closedGroups = this._windows[win.__SSi].closedGroups;
    let tabGroupState = lazy.TabGroupState.closed(tabGroup, win.__SSi);
    tabGroupState.tabs = this._collectClosedTabsForTabGroup(tabGroup.tabs, win);

    // TODO(jswinarton) it's unclear if updating lastClosedTabGroupCount is
    // necessary when restoring tab groups — it largely depends on how we
    // decide to do the restore.
    // To address in bug1915174
    this._windows[win.__SSi]._lastClosedTabGroupCount =
      tabGroupState.tabs.length;
    closedGroups.unshift(tabGroupState);
    this._closedObjectsChanged = true;
  },

  /**
   * Collect closed tab states for a tab group that is about to be
   * saved and/or closed.
   *
   * The `TabGroupState` module is generally responsible for collecting
   * tab group state data, but the session store has additional requirements
   * for closed tabs that are currently only implemented in
   * `SessionStoreInternal.maybeSaveClosedTab`. This method converts the tabs
   * in a tab group into the closed tab data schema format required for
   * closed or saved groups.
   *
   * @param {MozTabbrowserTab[]} tabs
   * @param {Window} win
   * @returns {ClosedTabStateData[]}
   */
  _collectClosedTabsForTabGroup(tabs, win) {
    let closedTabs = [];
    tabs.forEach(tab => {
      let tabState = lazy.TabState.collect(tab, TAB_CUSTOM_VALUES.get(tab));
      this.maybeSaveClosedTab(win, tab, tabState, {
        closedTabsArray: closedTabs,
        closedInTabGroup: true,
      });
    });
    return closedTabs;
  },

  /**
   * Flush and copy tab state when moving a tab to a new window.
   * @param aFromBrowser
   *        Browser reference.
   * @param aToBrowser
   *        Browser reference.
   */
  onMoveToNewWindow(aFromBrowser, aToBrowser) {
    lazy.TabStateFlusher.flush(aFromBrowser).then(() => {
      let tabState = lazy.TabStateCache.get(aFromBrowser.permanentKey);
      if (!tabState) {
        throw new Error(
          "Unexpected undefined tabState for onMoveToNewWindow aFromBrowser"
        );
      }
      lazy.TabStateCache.update(aToBrowser.permanentKey, tabState);
    });
  },

  /**
   * Save a closed tab if needed.
   *
   * @param {Window} aWindow
   *        Window reference.
   * @param {MozTabbrowserTab} aTab
   *        Tab reference.
   * @param {TabStateData} tabState
   *        Tab state.
   * @param {object} [options]
   * @param {TabStateData[]} [options.closedTabsArray]
   *        The array of closed tabs to save to. This could be a
   *        window's _closedTabs array or the tab list of a
   *        closed tab group.
   * @param {boolean} [options.closedInTabGroup=false]
   *        If this tab was closed due to the closing of a tab group.
   */
  maybeSaveClosedTab(
    aWindow,
    aTab,
    tabState,
    { closedTabsArray, closedInTabGroup = false } = {}
  ) {
    // Don't save private tabs
    let isPrivateWindow = PrivateBrowsingUtils.isWindowPrivate(aWindow);
    if (!isPrivateWindow && tabState.isPrivate) {
      return;
    }
    if (aTab == aWindow.FirefoxViewHandler.tab) {
      return;
    }

    let permanentKey = aTab.linkedBrowser.permanentKey;

    let tabData = {
      permanentKey,
      state: tabState,
      title: aTab.label,
      image: aWindow.gBrowser.getIcon(aTab),
      pos: aTab._tPos,
      closedAt: Date.now(),
      closedInGroup: aTab._closedInMultiselection,
      closedInTabGroupId: closedInTabGroup ? aTab.group.id : null,
      sourceWindowId: aWindow.__SSi,
    };

    let winData = this._windows[aWindow.__SSi];
    let closedTabs = closedTabsArray || winData._closedTabs;

    // Determine whether the tab contains any information worth saving. Note
    // that there might be pending state changes queued in the child that
    // didn't reach the parent yet. If a tab is emptied before closing then we
    // might still remove it from the list of closed tabs later.
    if (this._shouldSaveTabState(tabState)) {
      // Save the tab state, for now. We might push a valid tab out
      // of the list but those cases should be extremely rare and
      // do probably never occur when using the browser normally.
      // (Tests or add-ons might do weird things though.)
      this.saveClosedTabData(winData, closedTabs, tabData);
    }

    // Remember the closed tab to properly handle any last updates included in
    // the final "update" message sent by the frame script's unload handler.
    this._closingTabMap.set(permanentKey, {
      winData,
      closedTabs,
      tabData,
    });
  },

  /**
   * Remove listeners which were added when browser was inserted and reset restoring state.
   * Also re-instate lazy data and basically revert tab to its lazy browser state.
   * @param aTab
   *        Tab reference
   */
  resetBrowserToLazyState(aTab) {
    let browser = aTab.linkedBrowser;
    // Browser is already lazy so don't do anything.
    if (!browser.isConnected) {
      return;
    }

    this.cleanUpRemovedBrowser(aTab);

    aTab.setAttribute("pending", "true");

    this._lastKnownFrameLoader.delete(browser.permanentKey);
    this._crashedBrowsers.delete(browser.permanentKey);
    aTab.removeAttribute("crashed");

    let { userTypedValue = null, userTypedClear = 0 } = browser;
    let hasStartedLoad = browser.didStartLoadSinceLastUserTyping();

    let cacheState = lazy.TabStateCache.get(browser.permanentKey);

    // Cache the browser userTypedValue either if there is no cache state
    // at all (e.g. if it was already discarded before we got to cache its state)
    // or it may have been created but not including a userTypedValue (e.g.
    // for a private tab we will cache `isPrivate: true` as soon as the tab
    // is opened).
    //
    // But only if:
    //
    // - if there is no cache state yet (which is unfortunately required
    //   for tabs discarded immediately after creation by extensions, see
    //   Bug 1422588).
    //
    // - or the user typed value was already being loaded (otherwise the lazy
    //   tab will not be restored with the expected url once activated again,
    //   see Bug 1724205).
    let shouldUpdateCacheState =
      userTypedValue &&
      (!cacheState || (hasStartedLoad && !cacheState.userTypedValue));

    if (shouldUpdateCacheState) {
      // Discard was likely called before state can be cached.  Update
      // the persistent tab state cache with browser information so a
      // restore will be successful.  This information is necessary for
      // restoreTabContent in ContentRestore.sys.mjs to work properly.
      lazy.TabStateCache.update(browser.permanentKey, {
        userTypedValue,
        userTypedClear: 1,
      });
    }

    TAB_LAZY_STATES.set(aTab, {
      url: browser.currentURI.spec,
      title: aTab.label,
      userTypedValue,
      userTypedClear,
    });
  },

  /**
   * Check if we are dealing with a crashed browser. If so, then the corresponding
   * crashed tab was revived by navigating to a different page. Remove the browser
   * from the list of crashed browsers to stop ignoring its messages.
   * @param aBrowser
   *        Browser reference
   */
  maybeExitCrashedState(aBrowser) {
    let uri = aBrowser.documentURI;
    if (uri?.spec?.startsWith("about:tabcrashed")) {
      this._crashedBrowsers.delete(aBrowser.permanentKey);
    }
  },

  /**
   * A debugging-only function to check if a browser is in _crashedBrowsers.
   * @param aBrowser
   *        Browser reference
   */
  isBrowserInCrashedSet(aBrowser) {
    if (gDebuggingEnabled) {
      return this._crashedBrowsers.has(aBrowser.permanentKey);
    }
    throw new Error(
      "SessionStore.isBrowserInCrashedSet() should only be called in debug mode!"
    );
  },

  /**
   * When a tab is removed or suspended, remove listeners and reset restoring state.
   * @param aBrowser
   *        Browser reference
   */
  cleanUpRemovedBrowser(aTab) {
    let browser = aTab.linkedBrowser;

    browser.removeEventListener("SwapDocShells", this);
    browser.removeEventListener("oop-browser-crashed", this);
    browser.removeEventListener("oop-browser-buildid-mismatch", this);

    // If this tab was in the middle of restoring or still needs to be restored,
    // we need to reset that state. If the tab was restoring, we will attempt to
    // restore the next tab.
    let previousState = TAB_STATE_FOR_BROWSER.get(browser);
    if (previousState) {
      this._resetTabRestoringState(aTab);
      if (previousState == TAB_STATE_RESTORING) {
        this.restoreNextTab();
      }
    }
  },

  /**
   * Insert a given |tabData| object into the list of |closedTabs|. We will
   * determine the right insertion point based on the .closedAt properties of
   * all tabs already in the list. The list will be truncated to contain a
   * maximum of |this._max_tabs_undo| entries if required.
   *
   * @param {WindowStateData} winData
   * @param {ClosedTabStateData[]} closedTabs
   *   The list of closed tabs for a window or tab group.
   * @param {ClosedTabStateData} tabData
   *   The closed tab that should be inserted into `closedTabs`
   * @param {boolean} [saveAction=true]
   *   Whether or not to add an action to the closed actions stack on save.
   */
  saveClosedTabData(winData, closedTabs, tabData, saveAction = true) {
    // Find the index of the first tab in the list
    // of closed tabs that was closed before our tab.
    let index = closedTabs.findIndex(tab => {
      return tab.closedAt < tabData.closedAt;
    });

    // If we found no tab closed before our
    // tab then just append it to the list.
    if (index == -1) {
      index = closedTabs.length;
    }

    // About to save the closed tab, add a unique ID.
    tabData.closedId = this._nextClosedId++;

    // Insert tabData at the right position.
    closedTabs.splice(index, 0, tabData);
    this._closedObjectsChanged = true;

    if (tabData.closedInGroup) {
      if (winData._lastClosedTabGroupCount < this._max_tabs_undo) {
        if (winData._lastClosedTabGroupCount < 0) {
          winData._lastClosedTabGroupCount = 1;
        } else {
          winData._lastClosedTabGroupCount++;
        }
      }
    } else {
      winData._lastClosedTabGroupCount = -1;
    }

    winData.lastClosedTabGroupId = tabData.closedInTabGroupId || null;

    if (saveAction) {
      this._addClosedAction(this._LAST_ACTION_CLOSED_TAB, tabData.closedId);
    }

    // Truncate the list of closed tabs, if needed. For closed tabs within tab
    // groups, always keep all closed tabs because users expect tab groups to
    // be intact.
    if (
      !tabData.closedInTabGroupId &&
      closedTabs.length > this._max_tabs_undo
    ) {
      closedTabs.splice(this._max_tabs_undo, closedTabs.length);
    }
  },

  /**
   * Remove the closed tab data at |index| from the list of |closedTabs|. If
   * the tab's final message is still pending we will simply discard it when
   * it arrives so that the tab doesn't reappear in the list.
   *
   * @param winData (object)
   *        The data of the window.
   * @param index (uint)
   *        The index of the tab to remove.
   * @param closedTabs (array)
   *        The list of closed tabs for a window.
   */
  removeClosedTabData(winData, closedTabs, index) {
    // Remove the given index from the list.
    let [closedTab] = closedTabs.splice(index, 1);
    this._closedObjectsChanged = true;

    // If the tab is part of the last closed multiselected tab set,
    // we need to deduct the tab from the count.
    if (index < winData._lastClosedTabGroupCount) {
      winData._lastClosedTabGroupCount--;
    }

    // If the closed tab's state still has a .permanentKey property then we
    // haven't seen its final update message yet. Remove it from the map of
    // closed tabs so that we will simply discard its last messages and will
    // not add it back to the list of closed tabs again.
    if (closedTab.permanentKey) {
      this._closingTabMap.delete(closedTab.permanentKey);
      this._tabClosingByWindowMap.delete(closedTab.permanentKey);
      delete closedTab.permanentKey;
    }

    this._removeClosedAction(this._LAST_ACTION_CLOSED_TAB, closedTab.closedId);

    return closedTab;
  },

  /**
   * When a tab is selected, save session data
   * @param aWindow
   *        Window reference
   */
  onTabSelect: function ssi_onTabSelect(aWindow) {
    if (lazy.RunState.isRunning) {
      this._windows[aWindow.__SSi].selected =
        aWindow.gBrowser.tabContainer.selectedIndex;

      let tab = aWindow.gBrowser.selectedTab;
      let browser = tab.linkedBrowser;

      if (TAB_STATE_FOR_BROWSER.get(browser) == TAB_STATE_NEEDS_RESTORE) {
        // If BROWSER_STATE is still available for the browser and it is
        // If __SS_restoreState is still on the browser and it is
        // TAB_STATE_NEEDS_RESTORE, then we haven't restored this tab yet.
        //
        // It's possible that this tab was recently revived, and that
        // we've deferred showing the tab crashed page for it (if the
        // tab crashed in the background). If so, we need to re-enter
        // the crashed state, since we'll be showing the tab crashed
        // page.
        if (lazy.TabCrashHandler.willShowCrashedTab(browser)) {
          this.enterCrashedState(browser);
        } else {
          this.restoreTabContent(tab);
        }
      }
    }
  },

  onTabShow: function ssi_onTabShow(aWindow, aTab) {
    // If the tab hasn't been restored yet, move it into the right bucket
    if (
      TAB_STATE_FOR_BROWSER.get(aTab.linkedBrowser) == TAB_STATE_NEEDS_RESTORE
    ) {
      TabRestoreQueue.hiddenToVisible(aTab);

      // let's kick off tab restoration again to ensure this tab gets restored
      // with "restore_hidden_tabs" == false (now that it has become visible)
      this.restoreNextTab();
    }

    // Default delay of 2 seconds gives enough time to catch multiple TabShow
    // events. This used to be due to changing groups in 'tab groups'. We
    // might be able to get rid of this now?
    this.saveStateDelayed(aWindow);
  },

  onTabHide: function ssi_onTabHide(aWindow, aTab) {
    // If the tab hasn't been restored yet, move it into the right bucket
    if (
      TAB_STATE_FOR_BROWSER.get(aTab.linkedBrowser) == TAB_STATE_NEEDS_RESTORE
    ) {
      TabRestoreQueue.visibleToHidden(aTab);
    }

    // Default delay of 2 seconds gives enough time to catch multiple TabHide
    // events. This used to be due to changing groups in 'tab groups'. We
    // might be able to get rid of this now?
    this.saveStateDelayed(aWindow);
  },

  /**
   * Handler for the event that is fired when a <xul:browser> crashes.
   *
   * @param aWindow
   *        The window that the crashed browser belongs to.
   * @param aBrowser
   *        The <xul:browser> that is now in the crashed state.
   */
  onBrowserCrashed(aBrowser) {
    this.enterCrashedState(aBrowser);
    // The browser crashed so we might never receive flush responses.
    // Resolve all pending flush requests for the crashed browser.
    lazy.TabStateFlusher.resolveAll(aBrowser);
  },

  /**
   * Called when a browser is showing or is about to show the tab
   * crashed page. This method causes SessionStore to ignore the
   * tab until it's restored.
   *
   * @param browser
   *        The <xul:browser> that is about to show the crashed page.
   */
  enterCrashedState(browser) {
    this._crashedBrowsers.add(browser.permanentKey);

    let win = browser.ownerGlobal;

    // If we hadn't yet restored, or were still in the midst of
    // restoring this browser at the time of the crash, we need
    // to reset its state so that we can try to restore it again
    // when the user revives the tab from the crash.
    if (TAB_STATE_FOR_BROWSER.has(browser)) {
      let tab = win.gBrowser.getTabForBrowser(browser);
      if (tab) {
        this._resetLocalTabRestoringState(tab);
      }
    }
  },

  // Clean up data that has been closed a long time ago.
  // Do not reschedule a save. This will wait for the next regular
  // save.
  onIdleDaily() {
    // Remove old closed windows
    this._cleanupOldData([this._closedWindows]);

    // Remove closed tabs of closed windows
    this._cleanupOldData(
      this._closedWindows.map(winData => winData._closedTabs)
    );

    // Remove closed groups of closed windows
    this._cleanupOldData(
      this._closedWindows.map(winData => winData.closedGroups)
    );

    // Remove closed tabs of open windows
    this._cleanupOldData(
      Object.keys(this._windows).map(key => this._windows[key]._closedTabs)
    );

    // Remove closed groups of open windows
    this._cleanupOldData(
      Object.keys(this._windows).map(key => this._windows[key].closedGroups)
    );

    this._notifyOfClosedObjectsChange();
  },

  // Remove "old" data from an array
  _cleanupOldData(targets) {
    const TIME_TO_LIVE = this._prefBranch.getIntPref(
      "sessionstore.cleanup.forget_closed_after"
    );
    const now = Date.now();

    for (let array of targets) {
      for (let i = array.length - 1; i >= 0; --i) {
        let data = array[i];
        // Make sure that we have a timestamp to tell us when the target
        // has been closed. If we don't have a timestamp, default to a
        // safe timestamp: just now.
        data.closedAt = data.closedAt || now;
        if (now - data.closedAt > TIME_TO_LIVE) {
          array.splice(i, 1);
          this._closedObjectsChanged = true;
        }
      }
    }
  },

  /* ........ nsISessionStore API .............. */

  getBrowserState: function ssi_getBrowserState() {
    let state = this.getCurrentState();

    // Don't include the last session state in getBrowserState().
    delete state.lastSessionState;

    // Don't include any deferred initial state.
    delete state.deferredInitialState;

    return JSON.stringify(state);
  },

  setBrowserState: function ssi_setBrowserState(aState) {
    this._handleClosedWindows();

    try {
      var state = JSON.parse(aState);
    } catch (ex) {
      /* invalid state object - don't restore anything */
    }
    if (!state) {
      throw Components.Exception(
        "Invalid state string: not JSON",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    if (!state.windows) {
      throw Components.Exception("No windows", Cr.NS_ERROR_INVALID_ARG);
    }

    this._browserSetState = true;

    // Make sure the priority queue is emptied out
    this._resetRestoringState();

    var window = this._getTopWindow();
    if (!window) {
      this._restoreCount = 1;
      this._openWindowWithState(state);
      return;
    }

    // close all other browser windows
    for (let otherWin of this._browserWindows) {
      if (otherWin != window) {
        otherWin.close();
        this.onClose(otherWin);
      }
    }

    // make sure closed window data isn't kept
    if (this._closedWindows.length) {
      this._closedWindows = [];
      this._closedObjectsChanged = true;
    }

    // determine how many windows are meant to be restored
    this._restoreCount = state.windows ? state.windows.length : 0;

    // global data must be restored before restoreWindow is called so that
    // it happens before observers are notified
    this._globalState.setFromState(state);

    // Restore session cookies.
    lazy.SessionCookies.restore(state.cookies || []);

    // restore to the given state
    this.restoreWindows(window, state, { overwriteTabs: true });

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  /**
   * @param {Window} aWindow
   *        Window reference
   * @returns {{windows: WindowStateData[]}}
   */
  getWindowState: function ssi_getWindowState(aWindow) {
    if ("__SSi" in aWindow) {
      return Cu.cloneInto(this._getWindowState(aWindow), {});
    }

    if (DyingWindowCache.has(aWindow)) {
      let data = DyingWindowCache.get(aWindow);
      return Cu.cloneInto({ windows: [data] }, {});
    }

    throw Components.Exception(
      "Window is not tracked",
      Cr.NS_ERROR_INVALID_ARG
    );
  },

  setWindowState: function ssi_setWindowState(aWindow, aState, aOverwrite) {
    if (!aWindow.__SSi) {
      throw Components.Exception(
        "Window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    this.restoreWindows(aWindow, aState, { overwriteTabs: aOverwrite });

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  getTabState: function ssi_getTabState(aTab) {
    if (!aTab || !aTab.ownerGlobal) {
      throw Components.Exception("Need a valid tab", Cr.NS_ERROR_INVALID_ARG);
    }
    if (!aTab.ownerGlobal.__SSi) {
      throw Components.Exception(
        "Default view is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let tabState = lazy.TabState.collect(aTab, TAB_CUSTOM_VALUES.get(aTab));

    return JSON.stringify(tabState);
  },

  setTabState(aTab, aState) {
    // Remove the tab state from the cache.
    // Note that we cannot simply replace the contents of the cache
    // as |aState| can be an incomplete state that will be completed
    // by |restoreTabs|.
    let tabState = aState;
    if (typeof tabState == "string") {
      tabState = JSON.parse(aState);
    }
    if (!tabState) {
      throw Components.Exception(
        "Invalid state string: not JSON",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    if (typeof tabState != "object") {
      throw Components.Exception("Not an object", Cr.NS_ERROR_INVALID_ARG);
    }
    if (!("entries" in tabState)) {
      throw Components.Exception(
        "Invalid state object: no entries",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let window = aTab.ownerGlobal;
    if (!window || !("__SSi" in window)) {
      throw Components.Exception(
        "Window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    if (TAB_STATE_FOR_BROWSER.has(aTab.linkedBrowser)) {
      this._resetTabRestoringState(aTab);
    }

    this._ensureNoNullsInTabDataList(
      window.gBrowser.tabs,
      this._windows[window.__SSi].tabs,
      aTab._tPos
    );
    this.restoreTab(aTab, tabState);

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  getInternalObjectState(obj) {
    if (obj.__SSi) {
      return this._windows[obj.__SSi];
    }
    return obj.loadURI
      ? TAB_STATE_FOR_BROWSER.get(obj)
      : TAB_CUSTOM_VALUES.get(obj);
  },

  getObjectTypeForClosedId(aClosedId) {
    // check if matches a window first
    if (this.getClosedWindowDataByClosedId(aClosedId)) {
      return this._LAST_ACTION_CLOSED_WINDOW;
    }
    return this._LAST_ACTION_CLOSED_TAB;
  },

  /**
   * @param {number} aClosedId
   * @returns {WindowStateData|undefined}
   */
  getClosedWindowDataByClosedId: function ssi_getClosedWindowDataByClosedId(
    aClosedId
  ) {
    return this._closedWindows.find(
      closedData => closedData.closedId == aClosedId
    );
  },

  getWindowById: function ssi_getWindowById(aSessionStoreId) {
    let resultWindow;
    for (let window of this._browserWindows) {
      if (window.__SSi === aSessionStoreId) {
        resultWindow = window;
        break;
      }
    }
    return resultWindow;
  },

  duplicateTab: function ssi_duplicateTab(
    aWindow,
    aTab,
    aDelta = 0,
    aRestoreImmediately = true,
    { inBackground, index } = {}
  ) {
    if (!aTab || !aTab.ownerGlobal) {
      throw Components.Exception("Need a valid tab", Cr.NS_ERROR_INVALID_ARG);
    }
    if (!aTab.ownerGlobal.__SSi) {
      throw Components.Exception(
        "Default view is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    if (!aWindow.gBrowser) {
      throw Components.Exception(
        "Invalid window object: no gBrowser",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    // Create a new tab.
    let userContextId = aTab.getAttribute("usercontextid") || "";

    let tabOptions = {
      userContextId,
      index,
      ...(aTab == aWindow.gBrowser.selectedTab
        ? { relatedToCurrent: true, ownerTab: aTab }
        : {}),
      skipLoad: true,
      preferredRemoteType: aTab.linkedBrowser.remoteType,
    };
    let newTab = aWindow.gBrowser.addTrustedTab(null, tabOptions);

    // Start the throbber to pretend we're doing something while actually
    // waiting for data from the frame script. This throbber is disabled
    // if the URI is a local about: URI.
    let uriObj = aTab.linkedBrowser.currentURI;
    if (!uriObj || (uriObj && !uriObj.schemeIs("about"))) {
      newTab.setAttribute("busy", "true");
    }

    // Hack to ensure that the about:home, about:newtab, and about:welcome
    // favicon is loaded instantaneously, to avoid flickering and improve
    // perceived performance.
    aWindow.gBrowser.setDefaultIcon(newTab, uriObj);

    // Collect state before flushing.
    let tabState = lazy.TabState.collect(aTab, TAB_CUSTOM_VALUES.get(aTab));

    // Flush to get the latest tab state to duplicate.
    let browser = aTab.linkedBrowser;
    lazy.TabStateFlusher.flush(browser).then(() => {
      // The new tab might have been closed in the meantime.
      if (newTab.closing || !newTab.linkedBrowser) {
        return;
      }

      let window = newTab.ownerGlobal;

      // The tab or its window might be gone.
      if (!window || !window.__SSi) {
        return;
      }

      // Update state with flushed data. We can't use TabState.clone() here as
      // the tab to duplicate may have already been closed. In that case we
      // only have access to the <xul:browser>.
      let options = { includePrivateData: true };
      lazy.TabState.copyFromCache(browser.permanentKey, tabState, options);

      tabState.index += aDelta;
      tabState.index = Math.max(
        1,
        Math.min(tabState.index, tabState.entries.length)
      );
      tabState.pinned = false;

      if (inBackground === false) {
        aWindow.gBrowser.selectedTab = newTab;
      }

      // Restore the state into the new tab.
      this.restoreTab(newTab, tabState, {
        restoreImmediately: aRestoreImmediately,
      });
    });

    return newTab;
  },

  getWindows(aWindowOrOptions) {
    let isPrivate;
    if (!aWindowOrOptions) {
      aWindowOrOptions = this._getTopWindow();
    }
    if (aWindowOrOptions instanceof Ci.nsIDOMWindow) {
      isPrivate = PrivateBrowsingUtils.isBrowserPrivate(aWindowOrOptions);
    } else {
      isPrivate = Boolean(aWindowOrOptions.private);
    }

    const browserWindows = Array.from(this._browserWindows).filter(win => {
      return PrivateBrowsingUtils.isBrowserPrivate(win) === isPrivate;
    });
    return browserWindows;
  },

  getWindowForTabClosedId(aClosedId, aIncludePrivate) {
    // check non-private windows first, and then only check private windows if
    // aIncludePrivate was true
    const privateValues = aIncludePrivate ? [false, true] : [false];
    for (let privateness of privateValues) {
      for (let window of this.getWindows({ private: privateness })) {
        const windowState = this._windows[window.__SSi];
        const closedTabs =
          this._getStateForClosedTabsAndClosedGroupTabs(windowState);
        if (!closedTabs.length) {
          continue;
        }
        if (closedTabs.find(tab => tab.closedId === aClosedId)) {
          return window;
        }
      }
    }
    return undefined;
  },

  getLastClosedTabCount(aWindow) {
    if ("__SSi" in aWindow) {
      return Math.min(
        Math.max(this._windows[aWindow.__SSi]._lastClosedTabGroupCount, 1),
        this.getClosedTabCountForWindow(aWindow)
      );
    }

    throw (Components.returnCode = Cr.NS_ERROR_INVALID_ARG);
  },

  resetLastClosedTabCount(aWindow) {
    if ("__SSi" in aWindow) {
      this._windows[aWindow.__SSi]._lastClosedTabGroupCount = -1;
      this._windows[aWindow.__SSi].lastClosedTabGroupId = null;
    } else {
      throw (Components.returnCode = Cr.NS_ERROR_INVALID_ARG);
    }
  },

  getClosedTabCountForWindow: function ssi_getClosedTabCountForWindow(aWindow) {
    if ("__SSi" in aWindow) {
      return this._getStateForClosedTabsAndClosedGroupTabs(
        this._windows[aWindow.__SSi]
      ).length;
    }

    if (!DyingWindowCache.has(aWindow)) {
      throw Components.Exception(
        "Window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    return this._getStateForClosedTabsAndClosedGroupTabs(
      DyingWindowCache.get(aWindow)
    ).length;
  },

  _prepareClosedTabOptions(aOptions = {}) {
    const sourceOptions = Object.assign(
      {
        closedTabsFromAllWindows: this._closedTabsFromAllWindowsEnabled,
        closedTabsFromClosedWindows: this._closedTabsFromClosedWindowsEnabled,
        sourceWindow: null,
      },
      aOptions instanceof Ci.nsIDOMWindow
        ? { sourceWindow: aOptions }
        : aOptions
    );
    if (!sourceOptions.sourceWindow) {
      sourceOptions.sourceWindow = this._getTopWindow(sourceOptions.private);
    }
    /*
    _getTopWindow may return null on MacOS when the last window has been closed.
    Since private browsing windows are irrelevant after they have been closed we
    don't need to check if it was a private browsing window.
    */
    if (!sourceOptions.sourceWindow) {
      sourceOptions.private = false;
    }
    if (!sourceOptions.hasOwnProperty("private")) {
      sourceOptions.private = PrivateBrowsingUtils.isWindowPrivate(
        sourceOptions.sourceWindow
      );
    }
    return sourceOptions;
  },

  getClosedTabCount(aOptions) {
    const sourceOptions = this._prepareClosedTabOptions(aOptions);
    let tabCount = 0;

    if (sourceOptions.closedTabsFromAllWindows) {
      tabCount += this.getWindows({ private: sourceOptions.private })
        .map(win => this.getClosedTabCountForWindow(win))
        .reduce((total, count) => total + count, 0);
    } else {
      tabCount += this.getClosedTabCountForWindow(sourceOptions.sourceWindow);
    }

    if (!sourceOptions.private && sourceOptions.closedTabsFromClosedWindows) {
      tabCount += this.getClosedTabCountFromClosedWindows();
    }
    return tabCount;
  },

  getClosedTabCountFromClosedWindows:
    function ssi_getClosedTabCountFromClosedWindows() {
      const tabCount = this._closedWindows
        .map(
          winData =>
            this._getStateForClosedTabsAndClosedGroupTabs(winData).length
        )
        .reduce((total, count) => total + count, 0);
      return tabCount;
    },

  getClosedTabDataForWindow: function ssi_getClosedTabDataForWindow(aWindow) {
    return this._getClonedDataForWindow(
      aWindow,
      this._getStateForClosedTabsAndClosedGroupTabs
    );
  },

  getClosedTabData: function ssi_getClosedTabData(aOptions) {
    const sourceOptions = this._prepareClosedTabOptions(aOptions);
    const closedTabData = [];
    if (sourceOptions.closedTabsFromAllWindows) {
      for (let win of this.getWindows({ private: sourceOptions.private })) {
        closedTabData.push(...this.getClosedTabDataForWindow(win));
      }
    } else {
      closedTabData.push(
        ...this.getClosedTabDataForWindow(sourceOptions.sourceWindow)
      );
    }
    return closedTabData;
  },

  getClosedTabDataFromClosedWindows:
    function ssi_getClosedTabDataFromClosedWindows() {
      const closedTabData = [];
      for (let winData of this._closedWindows) {
        const sourceClosedId = winData.closedId;
        const closedTabs = Cu.cloneInto(
          this._getStateForClosedTabsAndClosedGroupTabs(winData),
          {}
        );
        // Add a property pointing back to the closed window source
        for (let tabData of closedTabs) {
          tabData.sourceClosedId = sourceClosedId;
        }
        closedTabData.push(...closedTabs);
      }
      // sorting is left to the caller
      return closedTabData;
    },

  /**
   * @param {Window|object} aOptions
   * @param {Window} [aOptions.sourceWindow]
   * @param {boolean} [aOptions.private = false]
   * @param {boolean} [aOptions.closedTabsFromAllWindows]
   * @param {boolean} [aOptions.closedTabsFromClosedWindows]
   * @returns {ClosedTabGroupStateData[]}
   */
  getClosedTabGroups: function ssi_getClosedTabGroups(aOptions) {
    const sourceOptions = this._prepareClosedTabOptions(aOptions);
    const closedTabGroups = [];
    if (sourceOptions.closedTabsFromAllWindows) {
      for (let win of this.getWindows({ private: sourceOptions.private })) {
        closedTabGroups.push(
          ...this._getClonedDataForWindow(win, w => w.closedGroups ?? [])
        );
      }
    } else if (sourceOptions.sourceWindow.closedGroups) {
      closedTabGroups.push(
        ...this._getClonedDataForWindow(
          sourceOptions.sourceWindow,
          w => w.closedGroups ?? []
        )
      );
    }

    if (sourceOptions.closedTabsFromClosedWindows) {
      for (let winData of this.getClosedWindowData()) {
        if (!winData.closedGroups) {
          continue;
        }
        // Add a property pointing back to the closed window source
        for (let groupData of winData.closedGroups) {
          for (let tabData of groupData.tabs) {
            tabData.sourceClosedId = winData.closedId;
          }
        }
        closedTabGroups.push(...winData.closedGroups);
      }
    }
    return closedTabGroups;
  },

  getLastClosedTabGroupId(aWindow) {
    if ("__SSi" in aWindow) {
      return this._windows[aWindow.__SSi].lastClosedTabGroupId;
    }

    throw new Error("Window is not tracked");
  },

  /**
   * Returns a clone of some subset of a window's state data.
   *
   * @template D
   * @param {Window} aWindow
   * @param {function(WindowStateData):D} selector
   *   A function that returns the desired data located within
   *   a supplied window state.
   * @returns {D}
   */
  _getClonedDataForWindow: function ssi_getClonedDataForWindow(
    aWindow,
    selector
  ) {
    // We need to enable wrapping reflectors in order to allow the cloning of
    // objects containing FormDatas, which could be stored by
    // form-associated custom elements.
    let options = { wrapReflectors: true };
    /** @type {WindowStateData} */
    let winData;

    if ("__SSi" in aWindow) {
      winData = this._windows[aWindow.__SSi];
    }

    if (!winData && !DyingWindowCache.has(aWindow)) {
      throw Components.Exception(
        "Window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    winData ??= DyingWindowCache.get(aWindow);
    let data = selector(winData);
    return Cu.cloneInto(data, {}, options);
  },

  /**
   * Returns either a unified list of closed tabs from both
   * `_closedTabs` and `closedGroups` or else, when supplying an index,
   * returns the specific closed tab from that unified list.
   *
   * This bridges the gap between callers that want a unified list of all closed tabs
   * from all contexts vs. callers that want a specific list of closed tabs from a
   * specific context (e.g. only closed tabs from a specific closed tab group).
   *
   * @param {WindowStateData} winData
   * @param {number} [aIndex]
   *   If not supplied, returns all closed tabs and tabs from closed tab groups.
   *   If supplied, returns the single closed tab with the given index.
   * @returns {TabStateData|TabStateData[]}
   */
  _getStateForClosedTabsAndClosedGroupTabs:
    function ssi_getStateForClosedTabsAndClosedGroupTabs(winData, aIndex) {
      const closedGroups = winData.closedGroups ?? [];
      const closedTabs = winData._closedTabs ?? [];

      // Merge tabs and groups into a single sorted array of tabs sorted by
      // closedAt
      let result = [];
      let groupIdx = 0;
      let tabIdx = 0;
      let current = 0;
      let totalLength = closedGroups.length + closedTabs.length;

      while (current < totalLength) {
        let group = closedGroups[groupIdx];
        let tab = closedTabs[tabIdx];

        if (
          groupIdx < closedGroups.length &&
          (tabIdx >= closedTabs.length || group?.closedAt > tab?.closedAt)
        ) {
          group.tabs.forEach((groupTab, idx) => {
            groupTab._originalStateIndex = idx;
            groupTab._originalGroupStateIndex = groupIdx;
            result.push(groupTab);
          });
          groupIdx++;
        } else {
          tab._originalStateIndex = tabIdx;
          result.push(tab);
          tabIdx++;
        }

        current++;
        if (current > aIndex) {
          break;
        }
      }

      if (aIndex !== undefined) {
        return result[aIndex];
      }

      return result;
    },

  /**
   * For a given closed tab that was retrieved by `_getStateForClosedTabsAndClosedGroupTabs`,
   * returns the specific closed tab list data source and the index within that data source
   * where the closed tab can be found.
   *
   * This bridges the gap between callers that want a unified list of all closed tabs
   * from all contexts vs. callers that want a specific list of closed tabs from a
   * specific context (e.g. only closed tabs from a specific closed tab group).
   *
   * @param {WindowState} sourceWinData
   * @param {TabStateData} tabState
   * @returns {{closedTabSet: TabStateData[], closedTabIndex: number}}
   */
  _getClosedTabStateFromUnifiedIndex: function ssi_getClosedTabForUnifiedIndex(
    sourceWinData,
    tabState
  ) {
    let closedTabSet, closedTabIndex;
    if (tabState._originalGroupStateIndex == null) {
      closedTabSet = sourceWinData._closedTabs;
    } else {
      closedTabSet =
        sourceWinData.closedGroups[tabState._originalGroupStateIndex].tabs;
    }
    closedTabIndex = tabState._originalStateIndex;

    return { closedTabSet, closedTabIndex };
  },

  undoCloseTab: function ssi_undoCloseTab(aSource, aIndex, aTargetWindow) {
    const sourceWinData = this._resolveClosedDataSource(aSource);
    const isPrivateSource = Boolean(sourceWinData.isPrivate);
    if (aTargetWindow && !aTargetWindow.__SSi) {
      throw Components.Exception(
        "Target window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    } else if (!aTargetWindow) {
      aTargetWindow = this._getTopWindow(isPrivateSource);
    }
    if (
      isPrivateSource !== PrivateBrowsingUtils.isWindowPrivate(aTargetWindow)
    ) {
      throw Components.Exception(
        "Target window doesn't have the same privateness as the source window",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    // default to the most-recently closed tab
    aIndex = aIndex || 0;

    const closedTabState = this._getStateForClosedTabsAndClosedGroupTabs(
      sourceWinData,
      aIndex
    );
    if (!closedTabState) {
      throw Components.Exception(
        "Invalid index: not in the closed tabs",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    let { closedTabSet, closedTabIndex } =
      this._getClosedTabStateFromUnifiedIndex(sourceWinData, closedTabState);

    // fetch the data of closed tab, while removing it from the array
    let { state, pos } = this.removeClosedTabData(
      sourceWinData,
      closedTabSet,
      closedTabIndex
    );
    this._cleanupOrphanedClosedGroups(sourceWinData);

    // Predict the remote type to use for the load to avoid unnecessary process
    // switches.
    let preferredRemoteType = lazy.E10SUtils.DEFAULT_REMOTE_TYPE;
    let url;
    if (state.entries?.length) {
      let activeIndex = (state.index || state.entries.length) - 1;
      activeIndex = Math.min(activeIndex, state.entries.length - 1);
      activeIndex = Math.max(activeIndex, 0);
      url = state.entries[activeIndex].url;
    }
    if (url) {
      preferredRemoteType = this.getPreferredRemoteType(
        url,
        aTargetWindow,
        state.userContextId
      );
    }

    // create a new tab
    let tabbrowser = aTargetWindow.gBrowser;
    let tab = (tabbrowser.selectedTab = tabbrowser.addTrustedTab(null, {
      // Append the tab if we're opening into a different window,
      index: aSource == aTargetWindow ? pos : Infinity,
      pinned: state.pinned,
      userContextId: state.userContextId,
      skipLoad: true,
      preferredRemoteType,
      tabGroup: tabbrowser.tabGroups.find(g => g.id == state.groupId),
    }));

    // restore tab content
    this.restoreTab(tab, state);

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();

    return tab;
  },

  undoClosedTabFromClosedWindow: function ssi_undoClosedTabFromClosedWindow(
    aSource,
    aClosedId,
    aTargetWindow
  ) {
    const sourceWinData = this._resolveClosedDataSource(aSource);
    const closedTabs =
      this._getStateForClosedTabsAndClosedGroupTabs(sourceWinData);
    const closedIndex = closedTabs.findIndex(
      tabData => tabData.closedId == aClosedId
    );
    if (closedIndex >= 0) {
      return this.undoCloseTab(aSource, closedIndex, aTargetWindow);
    }
    throw Components.Exception(
      "Invalid closedId: not in the closed tabs",
      Cr.NS_ERROR_INVALID_ARG
    );
  },

  getPreferredRemoteType(url, aWindow, userContextId) {
    return lazy.E10SUtils.getRemoteTypeForURI(
      url,
      aWindow.gMultiProcessBrowser,
      aWindow.gFissionBrowser,
      lazy.E10SUtils.DEFAULT_REMOTE_TYPE,
      null,
      lazy.E10SUtils.predictOriginAttributes({
        window: aWindow,
        userContextId,
      })
    );
  },

  /**
   * @param {Window|{sourceWindow: Window}|{sourceClosedId: number}|{sourceWindowId: string}} aSource
   * @returns {WindowStateData}
   */
  _resolveClosedDataSource(aSource) {
    let winData;
    if (aSource instanceof Ci.nsIDOMWindow) {
      winData = this.getWindowStateData(aSource);
    } else if (aSource.sourceWindow instanceof Ci.nsIDOMWindow) {
      winData = this.getWindowStateData(aSource.sourceWindow);
    } else if (typeof aSource.sourceClosedId == "number") {
      winData = this.getClosedWindowDataByClosedId(aSource.sourceClosedId);
      if (!winData) {
        throw Components.Exception(
          "No such closed window",
          Cr.NS_ERROR_INVALID_ARG
        );
      }
    } else if (typeof aSource.sourceWindowId == "string") {
      let win = this.getWindowById(aSource.sourceWindowId);
      winData = this.getWindowStateData(win);
    } else {
      throw Components.Exception(
        "Invalid source object",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    return winData;
  },

  forgetClosedTab: function ssi_forgetClosedTab(aSource, aIndex) {
    const winData = this._resolveClosedDataSource(aSource);
    // default to the most-recently closed tab
    aIndex = aIndex || 0;
    if (!(aIndex in winData._closedTabs)) {
      throw Components.Exception(
        "Invalid index: not in the closed tabs",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    // remove closed tab from the array
    this.removeClosedTabData(winData, winData._closedTabs, aIndex);

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  forgetClosedTabGroup: function ssi_forgetClosedTabGroup(aSource, tabGroupId) {
    const winData = this._resolveClosedDataSource(aSource);
    let closedGroupIndex = winData.closedGroups.findIndex(
      closedTabGroup => closedTabGroup.id == tabGroupId
    );
    // let closedTabGroup = this.getClosedTabGroup(aSource, tabGroupId);
    if (closedGroupIndex < 0) {
      throw Components.Exception(
        "Closed tab group not found",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let closedGroup = winData.closedGroups[closedGroupIndex];
    while (closedGroup.tabs.length) {
      this.removeClosedTabData(winData, closedGroup.tabs, 0);
    }
    winData.closedGroups.splice(closedGroupIndex, 1);

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  /**
   * @param {string} savedTabGroupId
   */
  forgetSavedTabGroup: function ssi_forgetSavedTabGroup(savedTabGroupId) {
    let savedGroupIndex = this._savedGroups.findIndex(
      savedTabGroup => savedTabGroup.id == savedTabGroupId
    );
    if (savedGroupIndex < 0) {
      throw Components.Exception(
        "Saved tab group not found",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let savedGroup = this._savedGroups[savedGroupIndex];
    for (let i = 0; i < savedGroup.tabs.length; i++) {
      this.removeClosedTabData({}, savedGroup.tabs, i);
    }
    this._savedGroups.splice(savedGroupIndex, 1);
    this._notifyOfSavedTabGroupsChange();

    // Notify of changes to closed objects.
    this._closedObjectsChanged = true;
    this._notifyOfClosedObjectsChange();
  },

  forgetClosedWindowById(aClosedId) {
    // We don't keep any record for closed private windows so privateness is not relevant here
    let closedIndex = this._closedWindows.findIndex(
      windowState => windowState.closedId == aClosedId
    );
    if (closedIndex < 0) {
      throw Components.Exception(
        "Invalid closedId: not in the closed windows",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    this.forgetClosedWindow(closedIndex);
  },

  forgetClosedTabById(aClosedId, aSourceOptions = {}) {
    let sourceWindowsData;
    let searchPrivateWindows = aSourceOptions.includePrivate ?? true;
    if (
      aSourceOptions instanceof Ci.nsIDOMWindow ||
      "sourceWindowId" in aSourceOptions ||
      "sourceClosedId" in aSourceOptions
    ) {
      sourceWindowsData = [this._resolveClosedDataSource(aSourceOptions)];
    } else {
      // Get the windows we'll look for the closed tab in, filtering out private
      // windows if necessary
      let browserWindows = Array.from(this._browserWindows);
      sourceWindowsData = [];
      for (let win of browserWindows) {
        if (
          !searchPrivateWindows &&
          PrivateBrowsingUtils.isBrowserPrivate(win)
        ) {
          continue;
        }
        sourceWindowsData.push(this._windows[win.__SSi]);
      }
    }

    // See if the aClosedId matches a closed tab in any window data
    for (let winData of sourceWindowsData) {
      let closedTabs = this._getStateForClosedTabsAndClosedGroupTabs(winData);
      let closedTabState = closedTabs.find(
        tabData => tabData.closedId == aClosedId
      );

      if (closedTabState) {
        let { closedTabSet, closedTabIndex } =
          this._getClosedTabStateFromUnifiedIndex(winData, closedTabState);
        // remove closed tab from the array
        this.removeClosedTabData(winData, closedTabSet, closedTabIndex);
        // Notify of changes to closed objects.
        this._notifyOfClosedObjectsChange();
        return;
      }
    }

    throw Components.Exception(
      "Invalid closedId: not found in the closed tabs of any window",
      Cr.NS_ERROR_INVALID_ARG
    );
  },

  getClosedWindowCount: function ssi_getClosedWindowCount() {
    return this._closedWindows.length;
  },

  /**
   * @returns {WindowStateData[]}
   */
  getClosedWindowData: function ssi_getClosedWindowData() {
    let closedWindows = Cu.cloneInto(this._closedWindows, {});
    for (let closedWinData of closedWindows) {
      this._trimSavedTabGroupMetadataInClosedWindow(closedWinData);
    }
    return closedWindows;
  },

  /**
   * If a closed window has a saved tab group inside of it, the closed window's
   * `groups` array entry will be a reference to a saved tab group entry.
   * However, since saved tab groups contain a lot of extra and duplicate
   * information, like their `tabs`, we only want to surface some of the
   * metadata about the saved tab groups to outside clients.
   *
   * @param {WindowStateData} closedWinData
   * @returns {void} mutates the argument `closedWinData`
   */
  _trimSavedTabGroupMetadataInClosedWindow(closedWinData) {
    let abbreviatedGroups = closedWinData.groups?.map(tabGroup =>
      lazy.TabGroupState.abbreviated(tabGroup)
    );
    closedWinData.groups = Cu.cloneInto(abbreviatedGroups, {});
  },

  maybeDontRestoreTabs(aWindow) {
    // Don't restore the tabs if we restore the session at startup
    this._windows[aWindow.__SSi]._maybeDontRestoreTabs = true;
  },

  isLastRestorableWindow() {
    return (
      Object.values(this._windows).filter(winData => !winData.isPrivate)
        .length == 1 &&
      !this._closedWindows.some(win => win._shouldRestore || false)
    );
  },

  undoCloseWindow: function ssi_undoCloseWindow(aIndex) {
    if (!(aIndex in this._closedWindows)) {
      throw Components.Exception(
        "Invalid index: not in the closed windows",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    // reopen the window
    let state = { windows: this._removeClosedWindow(aIndex) };
    delete state.windows[0].closedAt; // Window is now open.

    // If any saved tab groups are in the closed window, convert the saved tab
    // groups into open tab groups in the closed window and then forget the saved
    // tab groups. This should have the effect of "moving" the saved tab groups
    // into the window that's about to be restored.
    this._trimSavedTabGroupMetadataInClosedWindow(state.windows[0]);
    for (let tabGroup of state.windows[0].groups ?? []) {
      if (this.getSavedTabGroup(tabGroup.id)) {
        this.forgetSavedTabGroup(tabGroup.id);
      }
    }

    let window = this._openWindowWithState(state);
    this.windowToFocus = window;
    WINDOW_SHOWING_PROMISES.get(window).promise.then(win =>
      this.restoreWindows(win, state, { overwriteTabs: true })
    );

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();

    return window;
  },

  forgetClosedWindow: function ssi_forgetClosedWindow(aIndex) {
    // default to the most-recently closed window
    aIndex = aIndex || 0;
    if (!(aIndex in this._closedWindows)) {
      throw Components.Exception(
        "Invalid index: not in the closed windows",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    // remove closed window from the array
    let winData = this._closedWindows[aIndex];
    this._removeClosedWindow(aIndex);
    this._saveableClosedWindowData.delete(winData);

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  getCustomWindowValue(aWindow, aKey) {
    if ("__SSi" in aWindow) {
      let data = this._windows[aWindow.__SSi].extData || {};
      return data[aKey] || "";
    }

    if (DyingWindowCache.has(aWindow)) {
      let data = DyingWindowCache.get(aWindow).extData || {};
      return data[aKey] || "";
    }

    throw Components.Exception(
      "Window is not tracked",
      Cr.NS_ERROR_INVALID_ARG
    );
  },

  setCustomWindowValue(aWindow, aKey, aStringValue) {
    if (typeof aStringValue != "string") {
      throw new TypeError("setCustomWindowValue only accepts string values");
    }

    if (!("__SSi" in aWindow)) {
      throw Components.Exception(
        "Window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    if (!this._windows[aWindow.__SSi].extData) {
      this._windows[aWindow.__SSi].extData = {};
    }
    this._windows[aWindow.__SSi].extData[aKey] = aStringValue;
    this.saveStateDelayed(aWindow);
  },

  deleteCustomWindowValue(aWindow, aKey) {
    if (
      aWindow.__SSi &&
      this._windows[aWindow.__SSi].extData &&
      this._windows[aWindow.__SSi].extData[aKey]
    ) {
      delete this._windows[aWindow.__SSi].extData[aKey];
    }
    this.saveStateDelayed(aWindow);
  },

  getCustomTabValue(aTab, aKey) {
    return (TAB_CUSTOM_VALUES.get(aTab) || {})[aKey] || "";
  },

  setCustomTabValue(aTab, aKey, aStringValue) {
    if (typeof aStringValue != "string") {
      throw new TypeError("setCustomTabValue only accepts string values");
    }

    // If the tab hasn't been restored, then set the data there, otherwise we
    // could lose newly added data.
    if (!TAB_CUSTOM_VALUES.has(aTab)) {
      TAB_CUSTOM_VALUES.set(aTab, {});
    }

    TAB_CUSTOM_VALUES.get(aTab)[aKey] = aStringValue;
    this.saveStateDelayed(aTab.ownerGlobal);
  },

  deleteCustomTabValue(aTab, aKey) {
    let state = TAB_CUSTOM_VALUES.get(aTab);
    if (state && aKey in state) {
      delete state[aKey];
      this.saveStateDelayed(aTab.ownerGlobal);
    }
  },

  /**
   * Retrieves data specific to lazy-browser tabs.  If tab is not lazy,
   * will return undefined.
   *
   * @param aTab (xul:tab)
   *        The tabbrowser-tab the data is for.
   * @param aKey (string)
   *        The key which maps to the desired data.
   */
  getLazyTabValue(aTab, aKey) {
    return (TAB_LAZY_STATES.get(aTab) || {})[aKey];
  },

  getCustomGlobalValue(aKey) {
    return this._globalState.get(aKey);
  },

  setCustomGlobalValue(aKey, aStringValue) {
    if (typeof aStringValue != "string") {
      throw new TypeError("setCustomGlobalValue only accepts string values");
    }

    this._globalState.set(aKey, aStringValue);
    this.saveStateDelayed();
  },

  deleteCustomGlobalValue(aKey) {
    this._globalState.delete(aKey);
    this.saveStateDelayed();
  },

  /**
   * Undoes the closing of a tab or window which corresponds
   * to the closedId passed in.
   *
   * @param {integer} aClosedId
   *        The closedId of the tab or window
   * @param {boolean} [aIncludePrivate = true]
   *        Whether to restore private tabs or windows. Defaults to true
   * @param {Window} [aTargetWindow]
   *        When aClosedId is for a closed tab, which window to re-open the tab into.
   *        Defaults to current (topWindow).
   *
   * @returns a tab or window object
   */
  undoCloseById(aClosedId, aIncludePrivate = true, aTargetWindow) {
    // Check if we are re-opening a window first.
    for (let i = 0, l = this._closedWindows.length; i < l; i++) {
      if (this._closedWindows[i].closedId == aClosedId) {
        return this.undoCloseWindow(i);
      }
    }

    // See if the aCloseId matches a tab in an open window
    // Check for a tab.
    for (let sourceWindow of Services.wm.getEnumerator("navigator:browser")) {
      if (
        !aIncludePrivate &&
        PrivateBrowsingUtils.isWindowPrivate(sourceWindow)
      ) {
        continue;
      }
      let windowState = this._windows[sourceWindow.__SSi];
      if (windowState) {
        let closedTabs =
          this._getStateForClosedTabsAndClosedGroupTabs(windowState);
        for (let j = 0, l = closedTabs.length; j < l; j++) {
          if (closedTabs[j].closedId == aClosedId) {
            return this.undoCloseTab(sourceWindow, j, aTargetWindow);
          }
        }
      }
    }

    // Neither a tab nor a window was found, return undefined and let the caller decide what to do about it.
    return undefined;
  },

  /**
   * Updates the label and icon for a <xul:tab> using the data from
   * tabData.
   *
   * @param tab
   *        The <xul:tab> to update.
   * @param tabData (optional)
   *        The tabData to use to update the tab. If the argument is
   *        not supplied, the data will be retrieved from the cache.
   */
  updateTabLabelAndIcon(tab, tabData = null) {
    if (tab.hasAttribute("customizemode")) {
      return;
    }

    let browser = tab.linkedBrowser;
    let win = browser.ownerGlobal;

    if (!tabData) {
      tabData = lazy.TabState.collect(tab, TAB_CUSTOM_VALUES.get(tab));
      if (!tabData) {
        throw new Error("tabData not found for given tab");
      }
    }

    let activePageData = tabData.entries[tabData.index - 1] || null;

    // If the page has a title, set it.
    if (activePageData) {
      if (activePageData.title && activePageData.title != activePageData.url) {
        win.gBrowser.setInitialTabTitle(tab, activePageData.title, {
          isContentTitle: true,
        });
      } else {
        win.gBrowser.setInitialTabTitle(tab, activePageData.url);
      }
    }

    // Restore the tab icon.
    if ("image" in tabData) {
      // We know that about:blank is safe to load in any remote type. Since
      // SessionStore is triggered with about:blank, there must be a process
      // flip. We will ignore the first about:blank load to prevent resetting the
      // favicon that we have set earlier to avoid flickering and improve
      // perceived performance.
      if (
        !activePageData ||
        (activePageData && activePageData.url != "about:blank")
      ) {
        win.gBrowser.setIcon(
          tab,
          tabData.image,
          undefined,
          tabData.iconLoadingPrincipal
        );
      }
      lazy.TabStateCache.update(browser.permanentKey, {
        image: null,
        iconLoadingPrincipal: null,
      });
    }
  },

  // This method deletes all the closedTabs matching userContextId.
  _forgetTabsWithUserContextId(userContextId) {
    for (let window of Services.wm.getEnumerator("navigator:browser")) {
      let windowState = this._windows[window.__SSi];
      if (windowState) {
        // In order to remove the tabs in the correct order, we store the
        // indexes, into an array, then we revert the array and remove closed
        // data from the last one going backward.
        let indexes = [];
        windowState._closedTabs.forEach((closedTab, index) => {
          if (closedTab.state.userContextId == userContextId) {
            indexes.push(index);
          }
        });

        for (let index of indexes.reverse()) {
          this.removeClosedTabData(windowState, windowState._closedTabs, index);
        }
      }
    }

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  /**
   * Restores the session state stored in LastSession. This will attempt
   * to merge data into the current session. If a window was opened at startup
   * with pinned tab(s), then the remaining data from the previous session for
   * that window will be opened into that window. Otherwise new windows will
   * be opened.
   */
  restoreLastSession: function ssi_restoreLastSession() {
    // Use the public getter since it also checks PB mode
    if (!this.canRestoreLastSession) {
      throw Components.Exception("Last session can not be restored");
    }

    Services.obs.notifyObservers(null, NOTIFY_INITIATING_MANUAL_RESTORE);

    // First collect each window with its id...
    let windows = {};
    for (let window of this._browserWindows) {
      if (window.__SS_lastSessionWindowID) {
        windows[window.__SS_lastSessionWindowID] = window;
      }
    }

    let lastSessionState = LastSession.getState();

    // This shouldn't ever be the case...
    if (!lastSessionState.windows.length) {
      throw Components.Exception(
        "lastSessionState has no windows",
        Cr.NS_ERROR_UNEXPECTED
      );
    }

    // We're technically doing a restore, so set things up so we send the
    // notification when we're done. We want to send "sessionstore-browser-state-restored".
    this._restoreCount = lastSessionState.windows.length;
    this._browserSetState = true;

    // We want to re-use the last opened window instead of opening a new one in
    // the case where it's "empty" and not associated with a window in the session.
    // We will do more processing via _prepWindowToRestoreInto if we need to use
    // the lastWindow.
    let lastWindow = this._getTopWindow();
    let canUseLastWindow = lastWindow && !lastWindow.__SS_lastSessionWindowID;

    // global data must be restored before restoreWindow is called so that
    // it happens before observers are notified
    this._globalState.setFromState(lastSessionState);

    let openWindows = [];
    let windowsToOpen = [];

    // Restore session cookies.
    lazy.SessionCookies.restore(lastSessionState.cookies || []);

    // Restore into windows or open new ones as needed.
    for (let i = 0; i < lastSessionState.windows.length; i++) {
      let winState = lastSessionState.windows[i];

      // If we're restoring multiple times without
      // Firefox restarting, we need to remove
      // the window being restored from "previously closed windows"
      if (this._restoreWithoutRestart) {
        let restoreIndex = this._closedWindows.findIndex(win => {
          return win.closedId == winState.closedId;
        });
        if (restoreIndex > -1) {
          this._closedWindows.splice(restoreIndex, 1);
        }
      }

      let lastSessionWindowID = winState.__lastSessionWindowID;
      // delete lastSessionWindowID so we don't add that to the window again
      delete winState.__lastSessionWindowID;

      // See if we can use an open window. First try one that is associated with
      // the state we're trying to restore and then fallback to the last selected
      // window.
      let windowToUse = windows[lastSessionWindowID];
      if (!windowToUse && canUseLastWindow) {
        windowToUse = lastWindow;
        canUseLastWindow = false;
      }

      let [canUseWindow, canOverwriteTabs] =
        this._prepWindowToRestoreInto(windowToUse);

      // If there's a window already open that we can restore into, use that
      if (canUseWindow) {
        if (!PERSIST_SESSIONS) {
          // Since we're not overwriting existing tabs, we want to merge _closedTabs,
          // putting existing ones first. Then make sure we're respecting the max pref.
          if (winState._closedTabs && winState._closedTabs.length) {
            let curWinState = this._windows[windowToUse.__SSi];
            curWinState._closedTabs = curWinState._closedTabs.concat(
              winState._closedTabs
            );
            curWinState._closedTabs.splice(
              this._max_tabs_undo,
              curWinState._closedTabs.length
            );
          }
        }
        // We don't restore window right away, just store its data.
        // Later, these windows will be restored with newly opened windows.
        this._updateWindowRestoreState(windowToUse, {
          windows: [winState],
          options: { overwriteTabs: canOverwriteTabs },
        });
        openWindows.push(windowToUse);
      } else {
        windowsToOpen.push(winState);
      }
    }

    // Actually restore windows in reversed z-order.
    this._openWindows({ windows: windowsToOpen }).then(openedWindows =>
      this._restoreWindowsInReversedZOrder(openWindows.concat(openedWindows))
    );

    if (this._restoreWithoutRestart) {
      this.removeDuplicateClosedWindows(lastSessionState);
    }

    // Merge closed windows from this session with ones from last session
    if (lastSessionState._closedWindows) {
      // reset window closedIds and any references to them from closed tabs
      for (let closedWindow of lastSessionState._closedWindows) {
        closedWindow.closedId = this._nextClosedId++;
        if (closedWindow._closedTabs?.length) {
          this._resetClosedTabIds(
            closedWindow._closedTabs,
            closedWindow.closedId
          );
        }
      }
      this._closedWindows = this._closedWindows.concat(
        lastSessionState._closedWindows
      );
      this._capClosedWindows();
      this._closedObjectsChanged = true;
    }

    lazy.DevToolsShim.restoreDevToolsSession(lastSessionState);

    // When the deferred session was created, open tab groups were converted to saved groups.
    // Now that they have been restored, they need to be removed from the saved groups list.
    let groupsToRemove = this._savedGroups.filter(
      group => group.removeAfterRestore
    );
    for (let group of groupsToRemove) {
      this.forgetSavedTabGroup(group.id);
    }

    // Set data that persists between sessions
    this._recentCrashes =
      (lastSessionState.session && lastSessionState.session.recentCrashes) || 0;

    // Update the session start time using the restored session state.
    this._updateSessionStartTime(lastSessionState);

    LastSession.clear();

    // Notify of changes to closed objects.
    this._notifyOfClosedObjectsChange();
  },

  /**
   * There might be duplicates in these two arrays if we
   * restore multiple times without restarting in between.
   * We will keep the contents of the more recent _closedWindows array
   *
   * @param lastSessionState
   * An object containing information about the previous browsing session
   */
  removeDuplicateClosedWindows(lastSessionState) {
    // A set of closedIDs for the most recent list of closed windows
    let currentClosedIds = new Set(
      this._closedWindows.map(window => window.closedId)
    );

    // Remove closed windows that are present in both current and last session
    lastSessionState._closedWindows = lastSessionState._closedWindows.filter(
      win => !currentClosedIds.has(win.closedId)
    );
  },

  /**
   * Revive a crashed tab and restore its state from before it crashed.
   *
   * @param aTab
   *        A <xul:tab> linked to a crashed browser. This is a no-op if the
   *        browser hasn't actually crashed, or is not associated with a tab.
   *        This function will also throw if the browser happens to be remote.
   */
  reviveCrashedTab(aTab) {
    if (!aTab) {
      throw new Error(
        "SessionStore.reviveCrashedTab expected a tab, but got null."
      );
    }

    let browser = aTab.linkedBrowser;
    if (!this._crashedBrowsers.has(browser.permanentKey)) {
      return;
    }

    // Sanity check - the browser to be revived should not be remote
    // at this point.
    if (browser.isRemoteBrowser) {
      throw new Error(
        "SessionStore.reviveCrashedTab: " +
          "Somehow a crashed browser is still remote."
      );
    }

    // We put the browser at about:blank in case the user is
    // restoring tabs on demand. This way, the user won't see
    // a flash of the about:tabcrashed page after selecting
    // the revived tab.
    aTab.removeAttribute("crashed");

    browser.loadURI(lazy.blankURI, {
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal({
        userContextId: aTab.userContextId,
      }),
      remoteTypeOverride: lazy.E10SUtils.NOT_REMOTE,
    });

    let data = lazy.TabState.collect(aTab, TAB_CUSTOM_VALUES.get(aTab));
    this.restoreTab(aTab, data, {
      forceOnDemand: true,
    });
  },

  /**
   * Revive all crashed tabs and reset the crashed tabs count to 0.
   */
  reviveAllCrashedTabs() {
    for (let window of Services.wm.getEnumerator("navigator:browser")) {
      for (let tab of window.gBrowser.tabs) {
        this.reviveCrashedTab(tab);
      }
    }
  },

  /**
   * Retrieves the latest session history information for a tab. The cached data
   * is returned immediately, but a callback may be provided that supplies
   * up-to-date data when or if it is available. The callback is passed a single
   * argument with data in the same format as the return value.
   *
   * @param tab tab to retrieve the session history for
   * @param updatedCallback function to call with updated data as the single argument
   * @returns a object containing 'index' specifying the current index, and an
   * array 'entries' containing an object for each history item.
   */
  getSessionHistory(tab, updatedCallback) {
    if (updatedCallback) {
      lazy.TabStateFlusher.flush(tab.linkedBrowser).then(() => {
        let sessionHistory = this.getSessionHistory(tab);
        if (sessionHistory) {
          updatedCallback(sessionHistory);
        }
      });
    }

    // Don't continue if the tab was closed before TabStateFlusher.flush resolves.
    if (tab.linkedBrowser) {
      let tabState = lazy.TabState.collect(tab, TAB_CUSTOM_VALUES.get(tab));
      return { index: tabState.index - 1, entries: tabState.entries };
    }
    return null;
  },

  /**
   * See if aWindow is usable for use when restoring a previous session via
   * restoreLastSession. If usable, prepare it for use.
   *
   * @param aWindow
   *        the window to inspect & prepare
   * @returns [canUseWindow, canOverwriteTabs]
   *          canUseWindow: can the window be used to restore into
   *          canOverwriteTabs: all of the current tabs are home pages and we
   *                            can overwrite them
   */
  _prepWindowToRestoreInto: function ssi_prepWindowToRestoreInto(aWindow) {
    if (!aWindow) {
      return [false, false];
    }

    // We might be able to overwrite the existing tabs instead of just adding
    // the previous session's tabs to the end. This will be set if possible.
    let canOverwriteTabs = false;

    // Look at the open tabs in comparison to home pages. If all the tabs are
    // home pages then we'll end up overwriting all of them. Otherwise we'll
    // just close the tabs that match home pages. Tabs with the about:blank
    // URI will always be overwritten.
    let homePages = ["about:blank"];
    let removableTabs = [];
    let tabbrowser = aWindow.gBrowser;
    let startupPref = this._prefBranch.getIntPref("startup.page");
    if (startupPref == 1) {
      homePages = homePages.concat(lazy.HomePage.get(aWindow).split("|"));
    }

    for (let i = tabbrowser.pinnedTabCount; i < tabbrowser.tabs.length; i++) {
      let tab = tabbrowser.tabs[i];
      if (homePages.includes(tab.linkedBrowser.currentURI.spec)) {
        removableTabs.push(tab);
      }
    }

    if (
      tabbrowser.tabs.length > tabbrowser.visibleTabs.length &&
      tabbrowser.visibleTabs.length === removableTabs.length
    ) {
      // If all the visible tabs are also removable and the selected tab is hidden or removeable, we will later remove
      // all "removable" tabs causing the browser to automatically close because the only tab left is hidden.
      // To prevent the browser from automatically closing, we will leave one other visible tab open.
      removableTabs.shift();
    }

    if (tabbrowser.tabs.length == removableTabs.length) {
      canOverwriteTabs = true;
    } else {
      // If we're not overwriting all of the tabs, then close the home tabs.
      for (let i = removableTabs.length - 1; i >= 0; i--) {
        tabbrowser.removeTab(removableTabs.pop(), { animate: false });
      }
    }

    return [true, canOverwriteTabs];
  },

  /* ........ Saving Functionality .............. */

  /**
   * Store window dimensions, visibility, sidebar
   * @param aWindow
   *        Window reference
   */
  _updateWindowFeatures: function ssi_updateWindowFeatures(aWindow) {
    var winData = this._windows[aWindow.__SSi];

    WINDOW_ATTRIBUTES.forEach(function (aAttr) {
      winData[aAttr] = this._getWindowDimension(aWindow, aAttr);
    }, this);

    if (winData.sizemode != "minimized") {
      winData.sizemodeBeforeMinimized = winData.sizemode;
    }

    var hidden = WINDOW_HIDEABLE_FEATURES.filter(function (aItem) {
      return aWindow[aItem] && !aWindow[aItem].visible;
    });
    if (hidden.length) {
      winData.hidden = hidden.join(",");
    } else if (winData.hidden) {
      delete winData.hidden;
    }

    const sidebarUIState = aWindow.SidebarController.getUIState();
    if (sidebarUIState) {
      winData.sidebar = structuredClone(sidebarUIState);
    }

    let workspaceID = aWindow.getWorkspaceID();
    if (workspaceID) {
      winData.workspaceID = workspaceID;
    }
  },

  /**
   * gather session data as object
   * @param aUpdateAll
   *        Bool update all windows
   * @returns object
   */
  getCurrentState(aUpdateAll) {
    this._handleClosedWindows().then(() => {
      this._notifyOfClosedObjectsChange();
    });

    var activeWindow = this._getTopWindow();

    let timerId = Glean.sessionRestore.collectAllWindowsData.start();
    if (lazy.RunState.isRunning) {
      // update the data for all windows with activities since the last save operation.
      let index = 0;
      for (let window of this._orderedBrowserWindows) {
        if (!this._isWindowLoaded(window)) {
          // window data is still in _statesToRestore
          continue;
        }
        if (aUpdateAll || DirtyWindows.has(window) || window == activeWindow) {
          this._collectWindowData(window);
        } else {
          // always update the window features (whose change alone never triggers a save operation)
          this._updateWindowFeatures(window);
        }
        this._windows[window.__SSi].zIndex = ++index;
      }
      DirtyWindows.clear();
    }
    Glean.sessionRestore.collectAllWindowsData.stopAndAccumulate(timerId);

    // An array that at the end will hold all current window data.
    var total = [];
    // The ids of all windows contained in 'total' in the same order.
    var ids = [];
    // The number of window that are _not_ popups.
    var nonPopupCount = 0;
    var ix;

    // collect the data for all windows
    for (ix in this._windows) {
      if (this._windows[ix]._restoring || this._windows[ix].isTaskbarTab) {
        // window data is still in _statesToRestore
        continue;
      }
      total.push(this._windows[ix]);
      ids.push(ix);
      if (!this._windows[ix].isPopup) {
        nonPopupCount++;
      }
    }

    // collect the data for all windows yet to be restored
    for (ix in this._statesToRestore) {
      for (let winData of this._statesToRestore[ix].windows) {
        total.push(winData);
        if (!winData.isPopup) {
          nonPopupCount++;
        }
      }
    }

    // shallow copy this._closedWindows to preserve current state
    let lastClosedWindowsCopy = this._closedWindows.slice();

    if (AppConstants.platform != "macosx") {
      // If no non-popup browser window remains open, return the state of the last
      // closed window(s). We only want to do this when we're actually "ending"
      // the session.
      // XXXzpao We should do this for _restoreLastWindow == true, but that has
      //        its own check for popups. c.f. bug 597619
      if (
        nonPopupCount == 0 &&
        !!lastClosedWindowsCopy.length &&
        lazy.RunState.isQuitting
      ) {
        // prepend the last non-popup browser window, so that if the user loads more tabs
        // at startup we don't accidentally add them to a popup window
        do {
          total.unshift(lastClosedWindowsCopy.shift());
        } while (total[0].isPopup && lastClosedWindowsCopy.length);
      }
    }

    if (activeWindow) {
      this.activeWindowSSiCache = activeWindow.__SSi || "";
    }
    ix = ids.indexOf(this.activeWindowSSiCache);
    // We don't want to restore focus to a minimized window or a window which had all its
    // tabs stripped out (doesn't exist).
    if (ix != -1 && total[ix] && total[ix].sizemode == "minimized") {
      ix = -1;
    }

    let session = {
      lastUpdate: Date.now(),
      startTime: this._sessionStartTime,
      recentCrashes: this._recentCrashes,
    };

    let state = {
      version: ["sessionrestore", FORMAT_VERSION],
      windows: total,
      selectedWindow: ix + 1,
      _closedWindows: lastClosedWindowsCopy,
      savedGroups: this._savedGroups,
      session,
      global: this._globalState.getState(),
    };

    // Collect and store session cookies.
    state.cookies = lazy.SessionCookies.collect();

    lazy.DevToolsShim.saveDevToolsSession(state);

    // Persist the last session if we deferred restoring it
    if (LastSession.canRestore) {
      state.lastSessionState = LastSession.getState();
    }

    // If we were called by the SessionSaver and started with only a private
    // window we want to pass the deferred initial state to not lose the
    // previous session.
    if (this._deferredInitialState) {
      state.deferredInitialState = this._deferredInitialState;
    }

    return state;
  },

  /**
   * serialize session data for a window
   * @param {Window} aWindow
   *        Window reference
   * @returns {{windows: [WindowStateData]}}
   */
  _getWindowState: function ssi_getWindowState(aWindow) {
    if (!this._isWindowLoaded(aWindow)) {
      return this._statesToRestore[WINDOW_RESTORE_IDS.get(aWindow)];
    }

    if (lazy.RunState.isRunning) {
      this._collectWindowData(aWindow);
    }

    return { windows: [this._windows[aWindow.__SSi]] };
  },

  /**
   * Retrieves window data for an active session.
   *
   * @param {Window} aWindow
   * @returns {WindowStateData}
   * @throws {Error} if `aWindow` is not being managed in the session store.
   */
  getWindowStateData: function ssi_getWindowStateData(aWindow) {
    if (!aWindow.__SSi || !(aWindow.__SSi in this._windows)) {
      throw Components.Exception(
        "Window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    return this._windows[aWindow.__SSi];
  },

  /**
   * Gathers data about a window and its tabs, and updates its
   * entry in this._windows.
   *
   * @param aWindow
   *        Window references.
   * @returns a Map mapping the browser tabs from aWindow to the tab
   *          entry that was put into the window data in this._windows.
   */
  _collectWindowData: function ssi_collectWindowData(aWindow) {
    let tabMap = new Map();

    if (!this._isWindowLoaded(aWindow)) {
      return tabMap;
    }

    let tabbrowser = aWindow.gBrowser;
    let tabs = tabbrowser.tabs;
    /** @type {WindowStateData} */
    let winData = this._windows[aWindow.__SSi];
    let tabsData = (winData.tabs = []);

    // update the internal state data for this window
    for (let tab of tabs) {
      if (tab == aWindow.FirefoxViewHandler.tab) {
        continue;
      }
      let tabData = lazy.TabState.collect(tab, TAB_CUSTOM_VALUES.get(tab));
      tabMap.set(tab, tabData);
      tabsData.push(tabData);
    }

    // update tab group state for this window
    winData.groups = [];
    for (let tabGroup of aWindow.gBrowser.tabGroups) {
      let tabGroupData = lazy.TabGroupState.collect(tabGroup);
      winData.groups.push(tabGroupData);
    }

    let selectedIndex = tabbrowser.tabbox.selectedIndex + 1;
    // We don't store the Firefox View tab in Session Store, so if it was the last selected "tab" when
    // a window is closed, point to the first item in the tab strip instead (it will never be the Firefox View tab,
    // since it's only inserted into the tab strip after it's selected).
    if (aWindow.FirefoxViewHandler.tab?.selected) {
      selectedIndex = 1;
      winData.title = tabbrowser.tabs[0].label;
    }
    winData.selected = selectedIndex;

    this._updateWindowFeatures(aWindow);

    // Make sure we keep __SS_lastSessionWindowID around for cases like entering
    // or leaving PB mode.
    if (aWindow.__SS_lastSessionWindowID) {
      this._windows[aWindow.__SSi].__lastSessionWindowID =
        aWindow.__SS_lastSessionWindowID;
    }

    DirtyWindows.remove(aWindow);
    return tabMap;
  },

  /* ........ Restoring Functionality .............. */

  /**
   * Open windows with data
   *
   * @param root
   *        Windows data
   * @returns a promise resolved when all windows have been opened
   */
  _openWindows(root) {
    let windowsOpened = [];
    for (let winData of root.windows) {
      if (!winData || !winData.tabs || !winData.tabs[0]) {
        this._log.debug(`_openWindows, skipping window with no tabs data`);
        this._restoreCount--;
        continue;
      }
      windowsOpened.push(this._openWindowWithState({ windows: [winData] }));
    }
    let windowOpenedPromises = [];
    for (const openedWindow of windowsOpened) {
      let deferred = WINDOW_SHOWING_PROMISES.get(openedWindow);
      windowOpenedPromises.push(deferred.promise);
    }
    return Promise.all(windowOpenedPromises);
  },

  /** reset closedId's from previous sessions to ensure these IDs are unique
   * @param tabData
   *        an array of data to be restored
   * @param {String} windowId
   *        The SessionStore id for the window these tabs should be associated with
   * @returns the updated tabData array
   */
  _resetClosedTabIds(tabData, windowId) {
    for (let entry of tabData) {
      entry.closedId = this._nextClosedId++;
      entry.sourceWindowId = windowId;
    }
    return tabData;
  },
  /**
   * restore features to a single window
   * @param aWindow
   *        Window reference to the window to use for restoration
   * @param winData
   *        JS object
   * @param aOptions.overwriteTabs
   *        to overwrite existing tabs w/ new ones
   * @param aOptions.firstWindow
   *        if this is the first non-private window we're
   *        restoring in this session, that might open an
   *        external link as well
   */
  restoreWindow: function ssi_restoreWindow(aWindow, winData, aOptions = {}) {
    let overwriteTabs = aOptions && aOptions.overwriteTabs;
    let firstWindow = aOptions && aOptions.firstWindow;

    this.restoreSidebar(aWindow, winData.sidebar, winData.isPopup);

    // initialize window if necessary
    if (aWindow && (!aWindow.__SSi || !this._windows[aWindow.__SSi])) {
      this.onLoad(aWindow);
    }

    let timerId = Glean.sessionRestore.restoreWindow.start();

    // We're not returning from this before we end up calling restoreTabs
    // for this window, so make sure we send the SSWindowStateBusy event.
    this._sendWindowRestoringNotification(aWindow);
    this._setWindowStateBusy(aWindow);

    if (winData.workspaceID) {
      this._log.debug(`Moving window to workspace: ${winData.workspaceID}`);
      aWindow.moveToWorkspace(winData.workspaceID);
    }

    if (!winData.tabs) {
      winData.tabs = [];
      // don't restore a single blank tab when we've had an external
      // URL passed in for loading at startup (cf. bug 357419)
    } else if (
      firstWindow &&
      !overwriteTabs &&
      winData.tabs.length == 1 &&
      (!winData.tabs[0].entries || !winData.tabs[0].entries.length)
    ) {
      winData.tabs = [];
    }

    // See SessionStoreInternal.restoreTabs for a description of what
    // selectTab represents.
    let selectTab = 0;
    if (overwriteTabs) {
      selectTab = parseInt(winData.selected || 1, 10);
      selectTab = Math.max(selectTab, 1);
      selectTab = Math.min(selectTab, winData.tabs.length);
    }

    let tabbrowser = aWindow.gBrowser;

    // disable smooth scrolling while adding, moving, removing and selecting tabs
    let arrowScrollbox = tabbrowser.tabContainer.arrowScrollbox;
    let smoothScroll = arrowScrollbox.smoothScroll;
    arrowScrollbox.smoothScroll = false;

    // We need to keep track of the initially open tabs so that they
    // can be moved to the end of the restored tabs.
    let initialTabs;
    if (!overwriteTabs && firstWindow) {
      initialTabs = Array.from(tabbrowser.tabs);
    }

    // Get rid of tabs that aren't needed anymore.
    if (overwriteTabs) {
      for (let i = tabbrowser.browsers.length - 1; i >= 0; i--) {
        if (!tabbrowser.tabs[i].selected) {
          tabbrowser.removeTab(tabbrowser.tabs[i]);
        }
      }
    }

    let restoreTabsLazily =
      this._prefBranch.getBoolPref("sessionstore.restore_tabs_lazily") &&
      this._restore_on_demand;

    this._log.debug(
      `restoreWindow, will restore ${winData.tabs.length} tabs and ${
        winData.groups?.length ?? 0
      } tab groups, restoreTabsLazily: ${restoreTabsLazily}`
    );
    if (winData.tabs.length) {
      var tabs = tabbrowser.createTabsForSessionRestore(
        restoreTabsLazily,
        selectTab,
        winData.tabs,
        winData.groups ?? []
      );
      this._log.debug(
        `restoreWindow, createTabsForSessionRestore returned ${tabs.length} tabs`
      );
    }

    // Move the originally open tabs to the end.
    if (initialTabs) {
      let endPosition = tabbrowser.tabs.length - 1;
      for (let i = 0; i < initialTabs.length; i++) {
        tabbrowser.unpinTab(initialTabs[i]);
        tabbrowser.moveTabTo(initialTabs[i], {
          tabIndex: endPosition,
          forceUngrouped: true,
        });
      }
    }

    // We want to correlate the window with data from the last session, so
    // assign another id if we have one. Otherwise clear so we don't do
    // anything with it.
    delete aWindow.__SS_lastSessionWindowID;
    if (winData.__lastSessionWindowID) {
      aWindow.__SS_lastSessionWindowID = winData.__lastSessionWindowID;
    }

    if (overwriteTabs) {
      delete this._windows[aWindow.__SSi].extData;
    }

    // Restore cookies from legacy sessions, i.e. before bug 912717.
    lazy.SessionCookies.restore(winData.cookies || []);

    if (winData.extData) {
      if (!this._windows[aWindow.__SSi].extData) {
        this._windows[aWindow.__SSi].extData = {};
      }
      for (var key in winData.extData) {
        this._windows[aWindow.__SSi].extData[key] = winData.extData[key];
      }
    }

    let newClosedTabsData;
    if (winData._closedTabs) {
      newClosedTabsData = winData._closedTabs;
      this._resetClosedTabIds(newClosedTabsData, aWindow.__SSi);
    } else {
      newClosedTabsData = [];
    }

    let newLastClosedTabGroupCount = winData._lastClosedTabGroupCount || -1;

    if (overwriteTabs || firstWindow) {
      // Overwrite existing closed tabs data when overwriteTabs=true
      // or we're the first window to be restored.
      this._windows[aWindow.__SSi]._closedTabs = newClosedTabsData;
    } else if (this._max_tabs_undo > 0) {
      // We preserve tabs between sessions so we just want to filter out any previously open tabs that
      // were added to the _closedTabs list prior to restoreLastSession
      if (PERSIST_SESSIONS) {
        newClosedTabsData = this._windows[aWindow.__SSi]._closedTabs.filter(
          tab => !tab.removeAfterRestore
        );
      } else {
        newClosedTabsData = newClosedTabsData.concat(
          this._windows[aWindow.__SSi]._closedTabs
        );
      }

      // ... and make sure that we don't exceed the max number of closed tabs
      // we can restore.
      this._windows[aWindow.__SSi]._closedTabs = newClosedTabsData.slice(
        0,
        this._max_tabs_undo
      );
    }
    // Because newClosedTabsData are put in first, we need to
    // copy also the _lastClosedTabGroupCount.
    this._windows[aWindow.__SSi]._lastClosedTabGroupCount =
      newLastClosedTabGroupCount;

    // Copy over closed tab groups from the previous session,
    // and reset closed tab ids for tabs within each group.
    let newClosedTabGroupsData = winData.closedGroups || [];
    newClosedTabGroupsData.forEach(group => {
      this._resetClosedTabIds(group.tabs, aWindow.__SSi);
    });
    this._windows[aWindow.__SSi].closedGroups = newClosedTabGroupsData;
    this._windows[aWindow.__SSi].lastClosedTabGroupId =
      winData.lastClosedTabGroupId || null;

    if (!this._isWindowLoaded(aWindow)) {
      // from now on, the data will come from the actual window
      delete this._statesToRestore[WINDOW_RESTORE_IDS.get(aWindow)];
      WINDOW_RESTORE_IDS.delete(aWindow);
      delete this._windows[aWindow.__SSi]._restoring;
    }

    // Restore tabs, if any.
    if (winData.tabs.length) {
      this.restoreTabs(aWindow, tabs, winData.tabs, selectTab);
    }

    // set smoothScroll back to the original value
    arrowScrollbox.smoothScroll = smoothScroll;

    Glean.sessionRestore.restoreWindow.stopAndAccumulate(timerId);

    this._setWindowStateReady(aWindow);

    this._sendWindowRestoredNotification(aWindow);

    Services.obs.notifyObservers(aWindow, NOTIFY_SINGLE_WINDOW_RESTORED);

    this._sendRestoreCompletedNotifications();
  },

  /**
   * Prepare connection to host beforehand.
   *
   * @param tab
   *        Tab we are loading from.
   * @param url
   *        URL of a host.
   * @returns a flag indicates whether a connection has been made
   */
  prepareConnectionToHost(tab, url) {
    if (url && !url.startsWith("about:")) {
      let principal = Services.scriptSecurityManager.createNullPrincipal({
        userContextId: tab.userContextId,
      });
      let sc = Services.io.QueryInterface(Ci.nsISpeculativeConnect);
      let uri = Services.io.newURI(url);
      try {
        sc.speculativeConnect(uri, principal, null, false);
        return true;
      } catch (error) {
        // Can't setup speculative connection for this url.
        console.error(error);
        return false;
      }
    }
    return false;
  },

  /**
   * Make a connection to a host when users hover mouse on a tab.
   * This will also set a flag in the tab to prevent us from speculatively
   * connecting a second time.
   *
   * @param tab
   *        a tab to speculatively connect on mouse hover.
   */
  speculativeConnectOnTabHover(tab) {
    let tabState = TAB_LAZY_STATES.get(tab);
    if (tabState && !tabState.connectionPrepared) {
      let url = this.getLazyTabValue(tab, "url");
      let prepared = this.prepareConnectionToHost(tab, url);
      // This is used to test if a connection has been made beforehand.
      if (gDebuggingEnabled) {
        tab.__test_connection_prepared = prepared;
        tab.__test_connection_url = url;
      }
      // A flag indicate that we've prepared a connection for this tab and
      // if is called again, we shouldn't prepare another connection.
      tabState.connectionPrepared = true;
    }
  },

  /**
   * This function will restore window features and then restore window data.
   *
   * @param windows
   *        ordered array of windows to restore
   */
  _restoreWindowsFeaturesAndTabs(windows) {
    // First, we restore window features, so that when users start interacting
    // with a window, we don't steal the window focus.
    for (let window of windows) {
      let state = this._statesToRestore[WINDOW_RESTORE_IDS.get(window)];
      this.restoreWindowFeatures(window, state.windows[0]);
    }

    // Then we restore data into windows.
    for (let window of windows) {
      let state = this._statesToRestore[WINDOW_RESTORE_IDS.get(window)];
      this.restoreWindow(
        window,
        state.windows[0],
        state.options || { overwriteTabs: true }
      );
      WINDOW_RESTORE_ZINDICES.delete(window);
    }
  },

  /**
   * This function will restore window in reversed z-index, so that users will
   * be presented with most recently used window first.
   *
   * @param windows
   *        unordered array of windows to restore
   */
  _restoreWindowsInReversedZOrder(windows) {
    windows.sort(
      (a, b) =>
        (WINDOW_RESTORE_ZINDICES.get(a) || 0) -
        (WINDOW_RESTORE_ZINDICES.get(b) || 0)
    );

    this.windowToFocus = windows[0];
    this._restoreWindowsFeaturesAndTabs(windows);
  },

  /**
   * Restore multiple windows using the provided state.
   * @param aWindow
   *        Window reference to the first window to use for restoration.
   *        Additionally required windows will be opened.
   * @param aState
   *        JS object or JSON string
   * @param aOptions.overwriteTabs
   *        to overwrite existing tabs w/ new ones
   * @param aOptions.firstWindow
   *        if this is the first non-private window we're
   *        restoring in this session, that might open an
   *        external link as well
   */
  restoreWindows: function ssi_restoreWindows(aWindow, aState, aOptions = {}) {
    // initialize window if necessary
    if (aWindow && (!aWindow.__SSi || !this._windows[aWindow.__SSi])) {
      this.onLoad(aWindow);
    }

    let root;
    try {
      root = typeof aState == "string" ? JSON.parse(aState) : aState;
    } catch (ex) {
      // invalid state object - don't restore anything
      this._log.debug(`restoreWindows failed to parse ${typeof aState} state`);
      this._log.error(ex);
      this._sendRestoreCompletedNotifications();
      return;
    }

    // Restore closed windows if any.
    if (root._closedWindows) {
      this._closedWindows = root._closedWindows;
      // reset window closedIds and any references to them from closed tabs
      for (let closedWindow of this._closedWindows) {
        closedWindow.closedId = this._nextClosedId++;
        if (closedWindow._closedTabs?.length) {
          this._resetClosedTabIds(
            closedWindow._closedTabs,
            closedWindow.closedId
          );
        }
      }
      this._log.debug(`Restored ${this._closedWindows.length} closed windows`);
      this._closedObjectsChanged = true;
    }

    this._log.debug(
      `restoreWindows will restore ${root.windows?.length} windows`
    );
    // We're done here if there are no windows.
    if (!root.windows || !root.windows.length) {
      this._sendRestoreCompletedNotifications();
      return;
    }

    let firstWindowData = root.windows.splice(0, 1);
    // Store the restore state and restore option of the current window,
    // so that the window can be restored in reversed z-order.
    this._updateWindowRestoreState(aWindow, {
      windows: firstWindowData,
      options: aOptions,
    });

    // Begin the restoration: First open all windows in creation order. After all
    // windows have opened, we restore states to windows in reversed z-order.
    this._openWindows(root).then(windows => {
      // We want to add current window to opened window, so that this window will be
      // restored in reversed z-order. (We add the window to first position, in case
      // no z-indices are found, that window will be restored first.)
      windows.unshift(aWindow);

      this._restoreWindowsInReversedZOrder(windows);
    });

    lazy.DevToolsShim.restoreDevToolsSession(aState);
  },

  /**
   * Manage history restoration for a window
   * @param aWindow
   *        Window to restore the tabs into
   * @param aTabs
   *        Array of tab references
   * @param aTabData
   *        Array of tab data
   * @param aSelectTab
   *        Index of the tab to select. This is a 1-based index where "1"
   *        indicates the first tab should be selected, and "0" indicates that
   *        the currently selected tab will not be changed.
   */
  restoreTabs(aWindow, aTabs, aTabData, aSelectTab) {
    var tabbrowser = aWindow.gBrowser;

    let numTabsToRestore = aTabs.length;
    let numTabsInWindow = tabbrowser.tabs.length;
    let tabsDataArray = this._windows[aWindow.__SSi].tabs;

    // Update the window state in case we shut down without being notified.
    // Individual tab states will be taken care of by restoreTab() below.
    if (numTabsInWindow == numTabsToRestore) {
      // Remove all previous tab data.
      tabsDataArray.length = 0;
    } else {
      // Remove all previous tab data except tabs that should not be overriden.
      tabsDataArray.splice(numTabsInWindow - numTabsToRestore);
    }

    // Remove items from aTabData if there is no corresponding tab:
    if (numTabsInWindow < tabsDataArray.length) {
      tabsDataArray.length = numTabsInWindow;
    }

    // Ensure the tab data array has items for each of the tabs
    this._ensureNoNullsInTabDataList(
      tabbrowser.tabs,
      tabsDataArray,
      numTabsInWindow - 1
    );

    if (aSelectTab > 0 && aSelectTab <= aTabs.length) {
      // Update the window state in case we shut down without being notified.
      this._windows[aWindow.__SSi].selected = aSelectTab;
    }

    // If we restore the selected tab, make sure it goes first.
    let selectedIndex = aTabs.indexOf(tabbrowser.selectedTab);
    if (selectedIndex > -1) {
      this.restoreTab(tabbrowser.selectedTab, aTabData[selectedIndex]);
    }

    // Restore all tabs.
    for (let t = 0; t < aTabs.length; t++) {
      if (t != selectedIndex) {
        this.restoreTab(aTabs[t], aTabData[t]);
      }
    }
  },

  // In case we didn't collect/receive data for any tabs yet we'll have to
  // fill the array with at least empty tabData objects until |_tPos| or
  // we'll end up with |null| entries.
  _ensureNoNullsInTabDataList(tabElements, tabDataList, changedTabPos) {
    let initialDataListLength = tabDataList.length;
    if (changedTabPos < initialDataListLength) {
      return;
    }
    // Add items to the end.
    while (tabDataList.length < changedTabPos) {
      let existingTabEl = tabElements[tabDataList.length];
      tabDataList.push({
        entries: [],
        lastAccessed: existingTabEl.lastAccessed,
      });
    }
    // Ensure the pre-existing items are non-null.
    for (let i = 0; i < initialDataListLength; i++) {
      if (!tabDataList[i]) {
        let existingTabEl = tabElements[i];
        tabDataList[i] = {
          entries: [],
          lastAccessed: existingTabEl.lastAccessed,
        };
      }
    }
  },

  // Restores the given tab state for a given tab.
  restoreTab(tab, tabData, options = {}) {
    let browser = tab.linkedBrowser;

    if (TAB_STATE_FOR_BROWSER.has(browser)) {
      this._log.warn("Must reset tab before calling restoreTab.");
      return;
    }

    let loadArguments = options.loadArguments;
    let window = tab.ownerGlobal;
    let tabbrowser = window.gBrowser;
    let forceOnDemand = options.forceOnDemand;
    let isRemotenessUpdate = options.isRemotenessUpdate;

    let willRestoreImmediately =
      options.restoreImmediately || tabbrowser.selectedBrowser == browser;

    let isBrowserInserted = browser.isConnected;

    // Increase the busy state counter before modifying the tab.
    this._setWindowStateBusy(window);

    // It's important to set the window state to dirty so that
    // we collect their data for the first time when saving state.
    DirtyWindows.add(window);

    if (!tab.hasOwnProperty("_tPos")) {
      throw new Error(
        "Shouldn't be trying to restore a tab that has no position"
      );
    }
    // Update the tab state in case we shut down without being notified.
    this._windows[window.__SSi].tabs[tab._tPos] = tabData;

    // Prepare the tab so that it can be properly restored.  We'll also attach
    // a copy of the tab's data in case we close it before it's been restored.
    // Anything that dispatches an event to external consumers must happen at
    // the end of this method, to make sure that the tab/browser object is in a
    // reliable and consistent state.

    if (tabData.lastAccessed) {
      tab.updateLastAccessed(tabData.lastAccessed);
    }

    if (!tabData.entries) {
      tabData.entries = [];
    }
    if (tabData.extData) {
      TAB_CUSTOM_VALUES.set(tab, Cu.cloneInto(tabData.extData, {}));
    } else {
      TAB_CUSTOM_VALUES.delete(tab);
    }

    // Tab is now open.
    delete tabData.closedAt;

    // Ensure the index is in bounds.
    let activeIndex = (tabData.index || tabData.entries.length) - 1;
    activeIndex = Math.min(activeIndex, tabData.entries.length - 1);
    activeIndex = Math.max(activeIndex, 0);

    // Save the index in case we updated it above.
    tabData.index = activeIndex + 1;

    tab.setAttribute("pending", "true");

    // If we're restoring this tab, it certainly shouldn't be in
    // the ignored set anymore.
    this._crashedBrowsers.delete(browser.permanentKey);

    // If we're in the midst of performing a process flip, then we must
    // have initiated a navigation. This means that these userTyped*
    // values are now out of date.
    if (
      options.restoreContentReason ==
      RESTORE_TAB_CONTENT_REASON.NAVIGATE_AND_RESTORE
    ) {
      delete tabData.userTypedValue;
      delete tabData.userTypedClear;
    }

    // Update the persistent tab state cache with |tabData| information.
    lazy.TabStateCache.update(browser.permanentKey, {
      // NOTE: Copy the entries array shallowly, so as to not screw with the
      // original tabData's history when getting history updates.
      history: { entries: [...tabData.entries], index: tabData.index },
      scroll: tabData.scroll || null,
      storage: tabData.storage || null,
      formdata: tabData.formdata || null,
      disallow: tabData.disallow || null,
      userContextId: tabData.userContextId || 0,

      // This information is only needed until the tab has finished restoring.
      // When that's done it will be removed from the cache and we always
      // collect it in TabState._collectBaseTabData().
      image: tabData.image || "",
      iconLoadingPrincipal: tabData.iconLoadingPrincipal || null,
      searchMode: tabData.searchMode || null,
      userTypedValue: tabData.userTypedValue || "",
      userTypedClear: tabData.userTypedClear || 0,
    });

    // Restore tab attributes.
    if ("attributes" in tabData) {
      lazy.TabAttributes.set(tab, tabData.attributes);
    }

    if (isBrowserInserted) {
      // Start a new epoch to discard all frame script messages relating to a
      // previous epoch. All async messages that are still on their way to chrome
      // will be ignored and don't override any tab data set when restoring.
      let epoch = this.startNextEpoch(browser.permanentKey);

      // Ensure that the tab will get properly restored in the event the tab
      // crashes while restoring.  But don't set this on lazy browsers as
      // restoreTab will get called again when the browser is instantiated.
      TAB_STATE_FOR_BROWSER.set(browser, TAB_STATE_NEEDS_RESTORE);

      this._sendRestoreHistory(browser, {
        tabData,
        epoch,
        loadArguments,
        isRemotenessUpdate,
      });

      // This could cause us to ignore MAX_CONCURRENT_TAB_RESTORES a bit, but
      // it ensures each window will have its selected tab loaded.
      if (willRestoreImmediately) {
        this.restoreTabContent(tab, options);
      } else if (!forceOnDemand) {
        TabRestoreQueue.add(tab);
        // Check if a tab is in queue and will be restored
        // after the currently loading tabs. If so, prepare
        // a connection to host to speed up page loading.
        if (TabRestoreQueue.willRestoreSoon(tab)) {
          if (activeIndex in tabData.entries) {
            let url = tabData.entries[activeIndex].url;
            let prepared = this.prepareConnectionToHost(tab, url);
            if (gDebuggingEnabled) {
              tab.__test_connection_prepared = prepared;
              tab.__test_connection_url = url;
            }
          }
        }
        this.restoreNextTab();
      }
    } else {
      // TAB_LAZY_STATES holds data for lazy-browser tabs to proxy for
      // data unobtainable from the unbound browser.  This only applies to lazy
      // browsers and will be removed once the browser is inserted in the document.
      // This must preceed `updateTabLabelAndIcon` call for required data to be present.
      let url = "about:blank";
      let title = "";

      if (activeIndex in tabData.entries) {
        url = tabData.entries[activeIndex].url;
        title = tabData.entries[activeIndex].title || url;
      }
      TAB_LAZY_STATES.set(tab, {
        url,
        title,
        userTypedValue: tabData.userTypedValue || "",
        userTypedClear: tabData.userTypedClear || 0,
      });
    }

    // Most of tabData has been restored, now continue with restoring
    // attributes that may trigger external events.

    if (tabData.pinned) {
      tabbrowser.pinTab(tab);
    } else {
      tabbrowser.unpinTab(tab);
    }

    if (tabData.hidden) {
      tabbrowser.hideTab(tab);
    } else {
      tabbrowser.showTab(tab);
    }

    if (!!tabData.muted != browser.audioMuted) {
      tab.toggleMuteAudio(tabData.muteReason);
    }

    if (tab.hasAttribute("customizemode")) {
      window.gCustomizeMode.setTab(tab);
    }

    // Update tab label and icon to show something
    // while we wait for the messages to be processed.
    this.updateTabLabelAndIcon(tab, tabData);

    // Decrease the busy state counter after we're done.
    this._setWindowStateReady(window);
  },

  /**
   * Kicks off restoring the given tab.
   *
   * @param aTab
   *        the tab to restore
   * @param aOptions
   *        optional arguments used when performing process switch during load
   */
  restoreTabContent(aTab, aOptions = {}) {
    let loadArguments = aOptions.loadArguments;
    if (aTab.hasAttribute("customizemode") && !loadArguments) {
      return;
    }

    let browser = aTab.linkedBrowser;
    let window = aTab.ownerGlobal;
    let tabData = lazy.TabState.clone(aTab, TAB_CUSTOM_VALUES.get(aTab));
    let activeIndex = tabData.index - 1;
    let activePageData = tabData.entries[activeIndex] || null;
    let uri = activePageData ? activePageData.url || null : null;

    this.markTabAsRestoring(aTab);

    this._sendRestoreTabContent(browser, {
      loadArguments,
      isRemotenessUpdate: aOptions.isRemotenessUpdate,
      reason:
        aOptions.restoreContentReason || RESTORE_TAB_CONTENT_REASON.SET_STATE,
    });

    // Focus the tab's content area, unless the restore is for a new tab URL or
    // was triggered by a DocumentChannel process switch.
    if (
      aTab.selected &&
      !window.isBlankPageURL(uri) &&
      !aOptions.isRemotenessUpdate
    ) {
      browser.focus();
    }
  },

  /**
   * Marks a given pending tab as restoring.
   *
   * @param aTab
   *        the pending tab to mark as restoring
   */
  markTabAsRestoring(aTab) {
    let browser = aTab.linkedBrowser;
    if (TAB_STATE_FOR_BROWSER.get(browser) != TAB_STATE_NEEDS_RESTORE) {
      throw new Error("Given tab is not pending.");
    }

    // Make sure that this tab is removed from the priority queue.
    TabRestoreQueue.remove(aTab);

    // Increase our internal count.
    this._tabsRestoringCount++;

    // Set this tab's state to restoring
    TAB_STATE_FOR_BROWSER.set(browser, TAB_STATE_RESTORING);
    aTab.removeAttribute("pending");
    aTab.removeAttribute("discarded");
  },

  /**
   * This _attempts_ to restore the next available tab. If the restore fails,
   * then we will attempt the next one.
   * There are conditions where this won't do anything:
   *   if we're in the process of quitting
   *   if there are no tabs to restore
   *   if we have already reached the limit for number of tabs to restore
   */
  restoreNextTab: function ssi_restoreNextTab() {
    // If we call in here while quitting, we don't actually want to do anything
    if (lazy.RunState.isQuitting) {
      return;
    }

    // Don't exceed the maximum number of concurrent tab restores.
    if (this._tabsRestoringCount >= MAX_CONCURRENT_TAB_RESTORES) {
      return;
    }

    let tab = TabRestoreQueue.shift();
    if (tab) {
      this.restoreTabContent(tab);
    }
  },

  /**
   * Restore visibility and dimension features to a window
   * @param aWindow
   *        Window reference
   * @param aWinData
   *        Object containing session data for the window
   */
  restoreWindowFeatures: function ssi_restoreWindowFeatures(aWindow, aWinData) {
    var hidden = aWinData.hidden ? aWinData.hidden.split(",") : [];
    var isTaskbarTab =
      aWindow.document.documentElement.getAttribute("taskbartab");
    if (!isTaskbarTab) {
      WINDOW_HIDEABLE_FEATURES.forEach(function (aItem) {
        aWindow[aItem].visible = !hidden.includes(aItem);
      });
    }

    if (aWinData.isPopup) {
      this._windows[aWindow.__SSi].isPopup = true;
      if (aWindow.gURLBar) {
        aWindow.gURLBar.readOnly = true;
      }
    } else {
      delete this._windows[aWindow.__SSi].isPopup;
      if (aWindow.gURLBar && !isTaskbarTab) {
        aWindow.gURLBar.readOnly = false;
      }
    }

    aWindow.setTimeout(() => {
      this.restoreDimensions(
        aWindow,
        +(aWinData.width || 0),
        +(aWinData.height || 0),
        "screenX" in aWinData ? +aWinData.screenX : NaN,
        "screenY" in aWinData ? +aWinData.screenY : NaN,
        aWinData.sizemode || "",
        aWinData.sizemodeBeforeMinimized || ""
      );
      this.restoreSidebar(aWindow, aWinData.sidebar, aWinData.isPopup);
    }, 0);
  },

  /**
   * @param aWindow
   *        Window reference
   * @param aSidebar
   *        Object containing command (sidebarcommand/category) and styles
   */
  restoreSidebar(aWindow, aSidebar, isPopup) {
    if (!aSidebar || isPopup) {
      return;
    }
    aWindow.SidebarController.initializeUIState(aSidebar);
  },

  /**
   * Restore a window's dimensions
   * @param aWidth
   *        Window width in desktop pixels
   * @param aHeight
   *        Window height in desktop pixels
   * @param aLeft
   *        Window left in desktop pixels
   * @param aTop
   *        Window top in desktop pixels
   * @param aSizeMode
   *        Window size mode (eg: maximized)
   * @param aSizeModeBeforeMinimized
   *        Window size mode before window got minimized (eg: maximized)
   */
  restoreDimensions: function ssi_restoreDimensions(
    aWindow,
    aWidth,
    aHeight,
    aLeft,
    aTop,
    aSizeMode,
    aSizeModeBeforeMinimized
  ) {
    var win = aWindow;
    var _this = this;
    function win_(aName) {
      return _this._getWindowDimension(win, aName);
    }

    const dwu = win.windowUtils;
    // find available space on the screen where this window is being placed
    let screen = lazy.gScreenManager.screenForRect(
      aLeft,
      aTop,
      aWidth,
      aHeight
    );
    if (screen) {
      let screenLeft = {},
        screenTop = {},
        screenWidth = {},
        screenHeight = {};
      screen.GetAvailRectDisplayPix(
        screenLeft,
        screenTop,
        screenWidth,
        screenHeight
      );

      // We store aLeft / aTop (screenX/Y) in desktop pixels, see
      // _getWindowDimension.
      screenLeft = screenLeft.value;
      screenTop = screenTop.value;
      screenWidth = screenWidth.value;
      screenHeight = screenHeight.value;

      let screenBottom = screenTop + screenHeight;
      let screenRight = screenLeft + screenWidth;

      // NOTE: contentsScaleFactor is the desktopToDeviceScale of the screen.
      // Naming could be more consistent here.
      let cssToDesktopScale =
        screen.defaultCSSScaleFactor / screen.contentsScaleFactor;

      let winSlopX = win.screenEdgeSlopX * cssToDesktopScale;
      let winSlopY = win.screenEdgeSlopY * cssToDesktopScale;

      let minSlop = MIN_SCREEN_EDGE_SLOP * cssToDesktopScale;
      let slopX = Math.max(minSlop, winSlopX);
      let slopY = Math.max(minSlop, winSlopY);

      // Pull the window within the screen's bounds (allowing a little slop
      // for windows that may be deliberately placed with their border off-screen
      // as when Win10 "snaps" a window to the left/right edge -- bug 1276516).
      // First, ensure the left edge is large enough...
      if (aLeft < screenLeft - slopX) {
        aLeft = screenLeft - winSlopX;
      }
      // Then check the resulting right edge, and reduce it if necessary.
      let right = aLeft + aWidth * cssToDesktopScale;
      if (right > screenRight + slopX) {
        right = screenRight + winSlopX;
        // See if we can move the left edge leftwards to maintain width.
        if (aLeft > screenLeft) {
          aLeft = Math.max(
            right - aWidth * cssToDesktopScale,
            screenLeft - winSlopX
          );
        }
      }
      // Finally, update aWidth to account for the adjusted left and right
      // edges, and convert it back to CSS pixels on the target screen.
      aWidth = (right - aLeft) / cssToDesktopScale;

      // And do the same in the vertical dimension.
      if (aTop < screenTop - slopY) {
        aTop = screenTop - winSlopY;
      }
      let bottom = aTop + aHeight * cssToDesktopScale;
      if (bottom > screenBottom + slopY) {
        bottom = screenBottom + winSlopY;
        if (aTop > screenTop) {
          aTop = Math.max(
            bottom - aHeight * cssToDesktopScale,
            screenTop - winSlopY
          );
        }
      }
      aHeight = (bottom - aTop) / cssToDesktopScale;
    }

    // Suppress animations.
    dwu.suppressAnimation(true);

    // We want to make sure users will get their animations back in case an exception is thrown.
    try {
      // only modify those aspects which aren't correct yet
      if (
        !isNaN(aLeft) &&
        !isNaN(aTop) &&
        (aLeft != win_("screenX") || aTop != win_("screenY"))
      ) {
        // moveTo uses CSS pixels relative to aWindow, while aLeft and aRight
        // are on desktop pixels, undo the conversion we do in
        // _getWindowDimension.
        let desktopToCssScale =
          aWindow.desktopToDeviceScale / aWindow.devicePixelRatio;
        aWindow.moveTo(aLeft * desktopToCssScale, aTop * desktopToCssScale);
      }
      if (
        aWidth &&
        aHeight &&
        (aWidth != win_("width") || aHeight != win_("height")) &&
        !ChromeUtils.shouldResistFingerprinting("RoundWindowSize", null)
      ) {
        // Don't resize the window if it's currently maximized and we would
        // maximize it again shortly after.
        if (aSizeMode != "maximized" || win_("sizemode") != "maximized") {
          aWindow.resizeTo(aWidth, aHeight);
        }
      }
      this._windows[aWindow.__SSi].sizemodeBeforeMinimized =
        aSizeModeBeforeMinimized;
      if (
        aSizeMode &&
        win_("sizemode") != aSizeMode &&
        !ChromeUtils.shouldResistFingerprinting("RoundWindowSize", null)
      ) {
        switch (aSizeMode) {
          case "maximized":
            aWindow.maximize();
            break;
          case "minimized":
            if (aSizeModeBeforeMinimized == "maximized") {
              aWindow.maximize();
            }
            aWindow.minimize();
            break;
          case "normal":
            aWindow.restore();
            break;
        }
      }
      // since resizing/moving a window brings it to the foreground,
      // we might want to re-focus the last focused window
      if (this.windowToFocus) {
        this.windowToFocus.focus();
      }
    } finally {
      // Enable animations.
      dwu.suppressAnimation(false);
    }
  },

  /* ........ Disk Access .............. */

  /**
   * Save the current session state to disk, after a delay.
   *
   * @param aWindow (optional)
   *        Will mark the given window as dirty so that we will recollect its
   *        data before we start writing.
   */
  saveStateDelayed(aWindow = null) {
    if (aWindow) {
      DirtyWindows.add(aWindow);
    }

    lazy.SessionSaver.runDelayed();
  },

  /* ........ Auxiliary Functions .............. */

  /**
   * Remove a closed window from the list of closed windows and indicate that
   * the change should be notified.
   *
   * @param index
   *        The index of the window in this._closedWindows.
   *
   * @returns Array of closed windows.
   */
  _removeClosedWindow(index) {
    // remove all of the closed tabs from the _lastClosedActions list
    // before removing the window from it
    for (let closedTab of this._closedWindows[index]._closedTabs) {
      this._removeClosedAction(
        this._LAST_ACTION_CLOSED_TAB,
        closedTab.closedId
      );
    }
    this._removeClosedAction(
      this._LAST_ACTION_CLOSED_WINDOW,
      this._closedWindows[index].closedId
    );
    let windows = this._closedWindows.splice(index, 1);
    this._closedObjectsChanged = true;
    return windows;
  },

  /**
   * Notifies observers that the list of closed tabs and/or windows has changed.
   * Waits a tick to allow SessionStorage a chance to register the change.
   */
  _notifyOfClosedObjectsChange() {
    if (!this._closedObjectsChanged) {
      return;
    }
    this._closedObjectsChanged = false;
    lazy.setTimeout(() => {
      Services.obs.notifyObservers(null, NOTIFY_CLOSED_OBJECTS_CHANGED);
    }, 0);
  },

  /**
   * Notifies observers that the list of saved tab groups has changed.
   * Waits a tick to allow SessionStorage a chance to register the change.
   */
  _notifyOfSavedTabGroupsChange() {
    lazy.setTimeout(() => {
      Services.obs.notifyObservers(null, NOTIFY_SAVED_TAB_GROUPS_CHANGED);
    }, 0);
  },

  /**
   * Update the session start time and send a telemetry measurement
   * for the number of days elapsed since the session was started.
   *
   * @param state
   *        The session state.
   */
  _updateSessionStartTime: function ssi_updateSessionStartTime(state) {
    // Attempt to load the session start time from the session state
    if (state.session && state.session.startTime) {
      this._sessionStartTime = state.session.startTime;
    }
  },

  /**
   * Iterator that yields all currently opened browser windows.
   * (Might miss the most recent one.)
   * This list is in focus order, but may include minimized windows
   * before non-minimized windows.
   */
  _browserWindows: {
    *[Symbol.iterator]() {
      for (let window of lazy.BrowserWindowTracker.orderedWindows) {
        if (window.__SSi && !window.closed) {
          yield window;
        }
      }
    },
  },

  /**
   * Iterator that yields all currently opened browser windows,
   * with minimized windows last.
   * (Might miss the most recent window.)
   */
  _orderedBrowserWindows: {
    *[Symbol.iterator]() {
      let windows = lazy.BrowserWindowTracker.orderedWindows;
      windows.sort((a, b) => {
        if (
          a.windowState == a.STATE_MINIMIZED &&
          b.windowState != b.STATE_MINIMIZED
        ) {
          return 1;
        }
        if (
          a.windowState != a.STATE_MINIMIZED &&
          b.windowState == b.STATE_MINIMIZED
        ) {
          return -1;
        }
        return 0;
      });
      for (let window of windows) {
        if (window.__SSi && !window.closed) {
          yield window;
        }
      }
    },
  },

  /**
   * Returns most recent window
   * @param {boolean} [isPrivate]
   *        Optional boolean to get only non-private or private windows
   *        When omitted, we'll return whatever the top-most window is regardless of privateness
   * @returns Window reference
   */
  _getTopWindow: function ssi_getTopWindow(isPrivate) {
    const options = { allowPopups: true };
    if (typeof isPrivate !== "undefined") {
      options.private = isPrivate;
    }
    return lazy.BrowserWindowTracker.getTopWindow(options);
  },

  /**
   * Calls onClose for windows that are determined to be closed but aren't
   * destroyed yet, which would otherwise cause getBrowserState and
   * setBrowserState to treat them as open windows.
   */
  _handleClosedWindows: function ssi_handleClosedWindows() {
    let promises = [];
    for (let window of Services.wm.getEnumerator("navigator:browser")) {
      if (window.closed) {
        promises.push(this.onClose(window));
      }
    }
    return Promise.all(promises);
  },

  /**
   * Store a restore state of a window to this._statesToRestore. The window
   * will be given an id that can be used to get the restore state from
   * this._statesToRestore.
   *
   * @param window
   *        a reference to a window that has a state to restore
   * @param state
   *        an object containing session data
   */
  _updateWindowRestoreState(window, state) {
    // Store z-index, so that windows can be restored in reversed z-order.
    if ("zIndex" in state.windows[0]) {
      WINDOW_RESTORE_ZINDICES.set(window, state.windows[0].zIndex);
    }
    do {
      var ID = "window" + Math.random();
    } while (ID in this._statesToRestore);
    WINDOW_RESTORE_IDS.set(window, ID);
    this._statesToRestore[ID] = state;
  },

  /**
   * open a new browser window for a given session state
   * called when restoring a multi-window session
   * @param aState
   *        Object containing session data
   */
  _openWindowWithState: function ssi_openWindowWithState(aState) {
    var argString = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    argString.data = "";

    // Build feature string
    let features;
    let winState = aState.windows[0];
    if (winState.chromeFlags) {
      features = ["chrome", "suppressanimation"];
      let chromeFlags = winState.chromeFlags;
      const allFlags = Ci.nsIWebBrowserChrome.CHROME_ALL;
      const hasAll = (chromeFlags & allFlags) == allFlags;
      if (hasAll) {
        features.push("all");
      }
      for (let [flag, onValue, offValue] of CHROME_FLAGS_MAP) {
        if (hasAll && allFlags & flag) {
          continue;
        }
        let value = chromeFlags & flag ? onValue : offValue;
        if (value) {
          features.push(value);
        }
      }
    } else {
      // |chromeFlags| is not found. Fallbacks to the old method.
      features = ["chrome", "dialog=no", "suppressanimation"];
      let hidden = winState.hidden?.split(",") || [];
      if (!hidden.length) {
        features.push("all");
      } else {
        features.push("resizable");
        WINDOW_HIDEABLE_FEATURES.forEach(aFeature => {
          if (!hidden.includes(aFeature)) {
            features.push(WINDOW_OPEN_FEATURES_MAP[aFeature] || aFeature);
          }
        });
      }
    }
    WINDOW_ATTRIBUTES.forEach(aFeature => {
      // Use !isNaN as an easy way to ignore sizemode and check for numbers
      if (aFeature in winState && !isNaN(winState[aFeature])) {
        features.push(aFeature + "=" + winState[aFeature]);
      }
    });

    if (winState.isPrivate) {
      features.push("private");
    }

    this._log.debug(
      `Opening window with features: ${features.join(
        ","
      )}, argString: ${argString}.`
    );
    var window = Services.ww.openWindow(
      null,
      AppConstants.BROWSER_CHROME_URL,
      "_blank",
      features.join(","),
      argString
    );

    this._updateWindowRestoreState(window, aState);
    WINDOW_SHOWING_PROMISES.set(window, Promise.withResolvers());

    return window;
  },

  /**
   * whether the user wants to load any other page at startup
   * (except the homepage) - needed for determining whether to overwrite the current tabs
   * C.f.: nsBrowserContentHandler's defaultArgs implementation.
   * @returns bool
   */
  _isCmdLineEmpty: function ssi_isCmdLineEmpty(aWindow, aState) {
    var pinnedOnly =
      aState.windows &&
      aState.windows.every(win => win.tabs.every(tab => tab.pinned));

    let hasFirstArgument = aWindow.arguments && aWindow.arguments[0];
    if (!pinnedOnly) {
      let defaultArgs = Cc["@mozilla.org/browser/clh;1"].getService(
        Ci.nsIBrowserHandler
      ).defaultArgs;
      if (
        aWindow.arguments &&
        aWindow.arguments[0] &&
        aWindow.arguments[0] == defaultArgs
      ) {
        hasFirstArgument = false;
      }
    }

    return !hasFirstArgument;
  },

  /**
   * on popup windows, the AppWindow's attributes seem not to be set correctly
   * we use thus JSDOMWindow attributes for sizemode and normal window attributes
   * (and hope for reasonable values when maximized/minimized - since then
   * outerWidth/outerHeight aren't the dimensions of the restored window)
   * @param aWindow
   *        Window reference
   * @param aAttribute
   *        String sizemode | width | height | other window attribute
   * @returns string
   */
  _getWindowDimension: function ssi_getWindowDimension(aWindow, aAttribute) {
    if (aAttribute == "sizemode") {
      switch (aWindow.windowState) {
        case aWindow.STATE_FULLSCREEN:
        case aWindow.STATE_MAXIMIZED:
          return "maximized";
        case aWindow.STATE_MINIMIZED:
          return "minimized";
        default:
          return "normal";
      }
    }

    // We want to persist the size / position in normal state, so that
    // we can restore to them even if the window is currently maximized
    // or minimized. However, attributes on window object only reflect
    // the current state of the window, so when it isn't in the normal
    // sizemode, their values aren't what we want the window to restore
    // to. In that case, try to read from the attributes of the root
    // element first instead.
    if (aWindow.windowState != aWindow.STATE_NORMAL) {
      let docElem = aWindow.document.documentElement;
      let attr = parseInt(docElem.getAttribute(aAttribute), 10);
      if (attr) {
        if (aAttribute != "width" && aAttribute != "height") {
          return attr;
        }
        // Width and height attribute report the inner size, but we want
        // to store the outer size, so add the difference.
        let appWin = aWindow.docShell.treeOwner
          .QueryInterface(Ci.nsIInterfaceRequestor)
          .getInterface(Ci.nsIAppWindow);
        let diff =
          aAttribute == "width"
            ? appWin.outerToInnerWidthDifferenceInCSSPixels
            : appWin.outerToInnerHeightDifferenceInCSSPixels;
        return attr + diff;
      }
    }

    switch (aAttribute) {
      case "width":
        return aWindow.outerWidth;
      case "height":
        return aWindow.outerHeight;
      case "screenX":
      case "screenY":
        // We use desktop pixels rather than CSS pixels to store window
        // positions, see bug 1247335.  This allows proper multi-monitor
        // positioning in mixed-DPI situations.
        // screenX/Y are in CSS pixels for the current window, so, convert them
        // to desktop pixels.
        return (
          (aWindow[aAttribute] * aWindow.devicePixelRatio) /
          aWindow.desktopToDeviceScale
        );
      default:
        return aAttribute in aWindow ? aWindow[aAttribute] : "";
    }
  },

  /**
   * @param aState is a session state
   * @param aRecentCrashes is the number of consecutive crashes
   * @returns whether a restore page will be needed for the session state
   */
  _needsRestorePage: function ssi_needsRestorePage(aState, aRecentCrashes) {
    const SIX_HOURS_IN_MS = 6 * 60 * 60 * 1000;

    // don't display the page when there's nothing to restore
    let winData = aState.windows || null;
    if (!winData || !winData.length) {
      return false;
    }

    // don't wrap a single about:sessionrestore page
    if (
      this._hasSingleTabWithURL(winData, "about:sessionrestore") ||
      this._hasSingleTabWithURL(winData, "about:welcomeback")
    ) {
      return false;
    }

    // don't automatically restore in Safe Mode
    if (Services.appinfo.inSafeMode) {
      return true;
    }

    let max_resumed_crashes = this._prefBranch.getIntPref(
      "sessionstore.max_resumed_crashes"
    );
    let sessionAge =
      aState.session &&
      aState.session.lastUpdate &&
      Date.now() - aState.session.lastUpdate;

    let decision =
      max_resumed_crashes != -1 &&
      (aRecentCrashes > max_resumed_crashes ||
        (sessionAge && sessionAge >= SIX_HOURS_IN_MS));
    if (decision) {
      let key;
      if (aRecentCrashes > max_resumed_crashes) {
        if (sessionAge && sessionAge >= SIX_HOURS_IN_MS) {
          key = "shown_many_crashes_old_session";
        } else {
          key = "shown_many_crashes";
        }
      } else {
        key = "shown_old_session";
      }
      Glean.browserEngagement.sessionrestoreInterstitial[key].add(1);
    }
    return decision;
  },

  /**
   * @param aWinData is the set of windows in session state
   * @param aURL is the single URL we're looking for
   * @returns whether the window data contains only the single URL passed
   */
  _hasSingleTabWithURL(aWinData, aURL) {
    if (
      aWinData &&
      aWinData.length == 1 &&
      aWinData[0].tabs &&
      aWinData[0].tabs.length == 1 &&
      aWinData[0].tabs[0].entries &&
      aWinData[0].tabs[0].entries.length == 1
    ) {
      return aURL == aWinData[0].tabs[0].entries[0].url;
    }
    return false;
  },

  /**
   * Determine if the tab state we're passed is something we should save. This
   * is used when closing a tab, tab group, or closing a window with a single tab
   *
   * @param aTabState
   *        The current tab state
   * @returns boolean
   */
  _shouldSaveTabState: function ssi_shouldSaveTabState(aTabState) {
    // If the tab has only a transient about: history entry, no other
    // session history, and no userTypedValue, then we don't actually want to
    // store this tab's data.
    const entryUrl = aTabState.entries[0]?.url;
    return (
      entryUrl &&
      !(
        aTabState.entries.length == 1 &&
        (entryUrl == "about:blank" ||
          entryUrl == "about:home" ||
          entryUrl == "about:newtab" ||
          entryUrl == "about:privatebrowsing") &&
        !aTabState.userTypedValue
      )
    );
  },

  /**
   * Determine if a tab group should be saved based on whether any of its tabs
   * should be saved.
   *
   * @param {MozTabbrowserTabGroup} group the tab group to check
   * @returns {boolean} true if the group is saveable.
   */
  shouldSaveTabGroup: function ssi_shouldSaveTabGroup(group) {
    if (!group) {
      return false;
    }
    for (let tab of group.tabs) {
      let tabState = lazy.TabState.collect(tab);
      if (this._shouldSaveTabState(tabState)) {
        return true;
      }
    }
    return false;
  },

  /**
   * Determine if the tab state we're passed is something we should keep to be
   * reopened at session restore. This is used when we are saving the current
   * session state to disk. This method is very similar to _shouldSaveTabState,
   * however, "about:blank" and "about:newtab" tabs will still be saved to disk.
   *
   * @param aTabState
   *        The current tab state
   * @returns boolean
   */
  _shouldSaveTab: function ssi_shouldSaveTab(aTabState) {
    // If the tab has one of the following transient about: history entry, no
    // userTypedValue, and no customizemode attribute, then we don't actually
    // want to write this tab's data to disk.
    return (
      aTabState.userTypedValue ||
      (aTabState.attributes && aTabState.attributes.customizemode == "true") ||
      (aTabState.entries.length &&
        aTabState.entries[0].url != "about:privatebrowsing")
    );
  },

  /**
   * This is going to take a state as provided at startup (via
   * SessionStartup.state) and split it into 2 parts. The first part
   * (defaultState) will be a state that should still be restored at startup,
   * while the second part (state) is a state that should be saved for later.
   * defaultState is derived from a clone of startupState,
   * and will be comprised of:
   *   - windows with only pinned tabs,
   *   - window position information, and
   *   - saved groups, including groups that were open at last shutdown.
   *
   * defaultState will be restored at startup. state will be passed into
   * LastSession and will be kept in case the user explicitly wants
   * to restore the previous session (publicly exposed as restoreLastSession).
   *
   * @param state
   *        The startupState, presumably from SessionStartup.state
   * @returns [defaultState, state]
   */
  _prepDataForDeferredRestore: function ssi_prepDataForDeferredRestore(
    startupState
  ) {
    // Make sure that we don't modify the global state as provided by
    // SessionStartup.state.
    let state = Cu.cloneInto(startupState, {});
    let hasPinnedTabs = false;
    let defaultState = {
      windows: [],
      selectedWindow: 1,
      savedGroups: state.savedGroups || [],
    };
    state.selectedWindow = state.selectedWindow || 1;

    // Fixes bug1954488
    // This solves a case where a user had open tab groups and then quit and
    // restarted the browser at least twice. In this case the saved groups
    // would still be marked as removeAfterRestore groups even though there was
    // no longer an open group associated with them in the lastSessionState.
    // To fix this we clear this property if we see it on saved groups,
    // converting them into permanently saved groups.
    for (let group of defaultState.savedGroups) {
      delete group.removeAfterRestore;
    }

    // Look at each window, remove pinned tabs, adjust selectedindex,
    // remove window if necessary.
    for (let wIndex = 0; wIndex < state.windows.length; ) {
      let window = state.windows[wIndex];
      window.selected = window.selected || 1;
      // We're going to put the state of the window into this object, but for closedTabs
      // we want to preserve the original closedTabs since that will be saved as the lastSessionState
      let newWindowState = {
        tabs: [],
      };
      if (PERSIST_SESSIONS) {
        newWindowState._closedTabs = Cu.cloneInto(window._closedTabs, {});
        newWindowState.closedGroups = Cu.cloneInto(window.closedGroups, {});
      }

      // We want to preserve the sidebar if previously open in the window
      if (window.sidebar) {
        newWindowState.sidebar = window.sidebar;
      }

      let groupsToSave = new Map();
      for (let tIndex = 0; tIndex < window.tabs.length; ) {
        if (window.tabs[tIndex].pinned) {
          // Adjust window.selected
          if (tIndex + 1 < window.selected) {
            window.selected -= 1;
          } else if (tIndex + 1 == window.selected) {
            newWindowState.selected = newWindowState.tabs.length + 1;
          }
          // + 1 because the tab isn't actually in the array yet

          // Now add the pinned tab to our window
          newWindowState.tabs = newWindowState.tabs.concat(
            window.tabs.splice(tIndex, 1)
          );
          // We don't want to increment tIndex here.
          continue;
        } else if (window.tabs[tIndex].groupId) {
          // Convert any open groups into saved groups.
          let groupStateToSave = window.groups.find(
            groupState => groupState.id == window.tabs[tIndex].groupId
          );
          let groupToSave = groupsToSave.get(groupStateToSave.id);
          if (!groupToSave) {
            groupToSave =
              lazy.TabGroupState.savedInClosedWindow(groupStateToSave);
            // If the session is manually restored, these groups will be removed from the saved groups list
            // to prevent duplication.
            groupToSave.removeAfterRestore = true;
            groupsToSave.set(groupStateToSave.id, groupToSave);
          }
          let tabToAdd = window.tabs[tIndex];
          groupToSave.tabs.push(this._formatTabStateForSavedGroup(tabToAdd));
        } else if (!window.tabs[tIndex].hidden && PERSIST_SESSIONS) {
          // Add any previously open tabs that aren't pinned or hidden to the recently closed tabs list
          // which we want to persist between sessions; if the session is manually restored, they will
          // be filtered out of the closed tabs list (due to removeAfterRestore property) and reopened
          // per expected session restore behavior.

          let tabState = window.tabs[tIndex];

          // Ensure the index is in bounds.
          let activeIndex = tabState.index;
          activeIndex = Math.min(activeIndex, tabState.entries.length - 1);
          activeIndex = Math.max(activeIndex, 0);

          if (activeIndex in tabState.entries) {
            let title =
              tabState.entries[activeIndex].title ||
              tabState.entries[activeIndex].url;

            let tabData = {
              state: tabState,
              title,
              image: tabState.image,
              pos: tIndex,
              closedAt: Date.now(),
              closedInGroup: false,
              removeAfterRestore: true,
            };

            if (this._shouldSaveTabState(tabState)) {
              let closedTabsList = newWindowState._closedTabs;
              this.saveClosedTabData(window, closedTabsList, tabData, false);
            }
          }
        }
        tIndex++;
      }

      // Any tab groups that were in the tab strip at the end of the last
      // session should be saved. If any tab groups were present in both
      // saved groups and open groups in the last session, set the saved
      // group's `removeAfterRestore` so that if the last session is restored,
      // the group will be opened to the tab strip and removed from the list
      // of saved tab groups.
      groupsToSave.forEach(groupState => {
        const alreadySavedGroup = defaultState.savedGroups.find(
          existingGroup => existingGroup.id == groupState.id
        );
        if (alreadySavedGroup) {
          alreadySavedGroup.removeAfterRestore = true;
        } else {
          defaultState.savedGroups.push(groupState);
        }
      });

      hasPinnedTabs ||= !!newWindowState.tabs.length;

      // Only transfer over window attributes for pinned tabs, which has
      // already been extracted into newWindowState.tabs.
      if (newWindowState.tabs.length) {
        WINDOW_ATTRIBUTES.forEach(function (attr) {
          if (attr in window) {
            newWindowState[attr] = window[attr];
            delete window[attr];
          }
        });
        // We're just copying position data into the window for pinned tabs.
        // Not copying over:
        // - extData
        // - isPopup
        // - hidden

        // Assign a unique ID to correlate the window to be opened with the
        // remaining data
        window.__lastSessionWindowID = newWindowState.__lastSessionWindowID =
          "" + Date.now() + Math.random();
      }

      // If this newWindowState contains pinned tabs (stored in tabs) or
      // closed tabs, add it to the defaultState so they're available immediately.
      if (
        newWindowState.tabs.length ||
        (PERSIST_SESSIONS &&
          (newWindowState._closedTabs.length ||
            newWindowState.closedGroups.length))
      ) {
        defaultState.windows.push(newWindowState);
        // Remove the window from the state if it doesn't have any tabs
        if (!window.tabs.length) {
          if (wIndex + 1 <= state.selectedWindow) {
            state.selectedWindow -= 1;
          } else if (wIndex + 1 == state.selectedWindow) {
            defaultState.selectedIndex = defaultState.windows.length + 1;
          }

          state.windows.splice(wIndex, 1);
          // We don't want to increment wIndex here.
          continue;
        }
      }
      wIndex++;
    }

    if (hasPinnedTabs) {
      // Move cookies over from so that they're restored right away and pinned tabs will load correctly.
      defaultState.cookies = state.cookies;
      delete state.cookies;
    }
    // we return state here rather than startupState so as to avoid duplicating
    // pinned tabs that we add to the defaultState (when a user restores a session)
    return [defaultState, state];
  },

  _sendRestoreCompletedNotifications:
    function ssi_sendRestoreCompletedNotifications() {
      // not all windows restored, yet
      if (this._restoreCount > 1) {
        this._restoreCount--;
        this._log.warn(
          `waiting on ${this._restoreCount} windows to be restored before sending restore complete notifications.`
        );
        return;
      }

      // observers were already notified
      if (this._restoreCount == -1) {
        return;
      }

      // This was the last window restored at startup, notify observers.
      if (!this._browserSetState) {
        Services.obs.notifyObservers(null, NOTIFY_WINDOWS_RESTORED);
        this._log.debug(`All ${this._restoreCount} windows restored`);
        this._deferredAllWindowsRestored.resolve();
      } else {
        // _browserSetState is used only by tests, and it uses an alternate
        // notification in order not to retrigger startup observers that
        // are listening for NOTIFY_WINDOWS_RESTORED.
        Services.obs.notifyObservers(null, NOTIFY_BROWSER_STATE_RESTORED);
      }

      this._browserSetState = false;
      this._restoreCount = -1;
    },

  /**
   * Set the given window's busy state
   * @param aWindow the window
   * @param aValue the window's busy state
   */
  _setWindowStateBusyValue: function ssi_changeWindowStateBusyValue(
    aWindow,
    aValue
  ) {
    this._windows[aWindow.__SSi].busy = aValue;

    // Keep the to-be-restored state in sync because that is returned by
    // getWindowState() as long as the window isn't loaded, yet.
    if (!this._isWindowLoaded(aWindow)) {
      let stateToRestore =
        this._statesToRestore[WINDOW_RESTORE_IDS.get(aWindow)].windows[0];
      stateToRestore.busy = aValue;
    }
  },

  /**
   * Set the given window's state to 'not busy'.
   * @param aWindow the window
   */
  _setWindowStateReady: function ssi_setWindowStateReady(aWindow) {
    let newCount = (this._windowBusyStates.get(aWindow) || 0) - 1;
    if (newCount < 0) {
      throw new Error("Invalid window busy state (less than zero).");
    }
    this._windowBusyStates.set(aWindow, newCount);

    if (newCount == 0) {
      this._setWindowStateBusyValue(aWindow, false);
      this._sendWindowStateReadyEvent(aWindow);
    }
  },

  /**
   * Set the given window's state to 'busy'.
   * @param aWindow the window
   */
  _setWindowStateBusy: function ssi_setWindowStateBusy(aWindow) {
    let newCount = (this._windowBusyStates.get(aWindow) || 0) + 1;
    this._windowBusyStates.set(aWindow, newCount);

    if (newCount == 1) {
      this._setWindowStateBusyValue(aWindow, true);
      this._sendWindowStateBusyEvent(aWindow);
    }
  },

  /**
   * Dispatch an SSWindowStateReady event for the given window.
   * @param aWindow the window
   */
  _sendWindowStateReadyEvent: function ssi_sendWindowStateReadyEvent(aWindow) {
    let event = aWindow.document.createEvent("Events");
    event.initEvent("SSWindowStateReady", true, false);
    aWindow.dispatchEvent(event);
  },

  /**
   * Dispatch an SSWindowStateBusy event for the given window.
   * @param aWindow the window
   */
  _sendWindowStateBusyEvent: function ssi_sendWindowStateBusyEvent(aWindow) {
    let event = aWindow.document.createEvent("Events");
    event.initEvent("SSWindowStateBusy", true, false);
    aWindow.dispatchEvent(event);
  },

  /**
   * Dispatch the SSWindowRestoring event for the given window.
   * @param aWindow
   *        The window which is going to be restored
   */
  _sendWindowRestoringNotification(aWindow) {
    let event = aWindow.document.createEvent("Events");
    event.initEvent("SSWindowRestoring", true, false);
    aWindow.dispatchEvent(event);
  },

  /**
   * Dispatch the SSWindowRestored event for the given window.
   * @param aWindow
   *        The window which has been restored
   */
  _sendWindowRestoredNotification(aWindow) {
    let event = aWindow.document.createEvent("Events");
    event.initEvent("SSWindowRestored", true, false);
    aWindow.dispatchEvent(event);
  },

  /**
   * Dispatch the SSTabRestored event for the given tab.
   * @param aTab
   *        The tab which has been restored
   * @param aIsRemotenessUpdate
   *        True if this tab was restored due to flip from running from
   *        out-of-main-process to in-main-process or vice-versa.
   */
  _sendTabRestoredNotification(aTab, aIsRemotenessUpdate) {
    let event = aTab.ownerDocument.createEvent("CustomEvent");
    event.initCustomEvent("SSTabRestored", true, false, {
      isRemotenessUpdate: aIsRemotenessUpdate,
    });
    aTab.dispatchEvent(event);
  },

  /**
   * @param aWindow
   *        Window reference
   * @returns whether this window's data is still cached in _statesToRestore
   *          because it's not fully loaded yet
   */
  _isWindowLoaded: function ssi_isWindowLoaded(aWindow) {
    return !WINDOW_RESTORE_IDS.has(aWindow);
  },

  /**
   * Resize this._closedWindows to the value of the pref, except in the case
   * where we don't have any non-popup windows on Windows and Linux. Then we must
   * resize such that we have at least one non-popup window.
   */
  _capClosedWindows: function ssi_capClosedWindows() {
    if (this._closedWindows.length <= this._max_windows_undo) {
      return;
    }
    let spliceTo = this._max_windows_undo;
    if (AppConstants.platform != "macosx") {
      let normalWindowIndex = 0;
      // try to find a non-popup window in this._closedWindows
      while (
        normalWindowIndex < this._closedWindows.length &&
        !!this._closedWindows[normalWindowIndex].isPopup
      ) {
        normalWindowIndex++;
      }
      if (normalWindowIndex >= this._max_windows_undo) {
        spliceTo = normalWindowIndex + 1;
      }
    }
    if (spliceTo < this._closedWindows.length) {
      this._closedWindows.splice(spliceTo, this._closedWindows.length);
      this._closedObjectsChanged = true;
    }
  },

  /**
   * Clears the set of windows that are "resurrected" before writing to disk to
   * make closing windows one after the other until shutdown work as expected.
   *
   * This function should only be called when we are sure that there has been
   * a user action that indicates the browser is actively being used and all
   * windows that have been closed before are not part of a series of closing
   * windows.
   */
  _clearRestoringWindows: function ssi_clearRestoringWindows() {
    for (let i = 0; i < this._closedWindows.length; i++) {
      delete this._closedWindows[i]._shouldRestore;
    }
  },

  /**
   * Reset state to prepare for a new session state to be restored.
   */
  _resetRestoringState: function ssi_initRestoringState() {
    TabRestoreQueue.reset();
    this._tabsRestoringCount = 0;
  },

  /**
   * Reset the restoring state for a particular tab. This will be called when
   * removing a tab or when a tab needs to be reset (it's being overwritten).
   *
   * @param aTab
   *        The tab that will be "reset"
   */
  _resetLocalTabRestoringState(aTab) {
    let browser = aTab.linkedBrowser;

    // Keep the tab's previous state for later in this method
    let previousState = TAB_STATE_FOR_BROWSER.get(browser);

    if (!previousState) {
      console.error("Given tab is not restoring.");
      return;
    }

    // The browser is no longer in any sort of restoring state.
    TAB_STATE_FOR_BROWSER.delete(browser);

    this._restoreListeners.get(browser.permanentKey)?.unregister();
    browser.browsingContext.clearRestoreState();

    aTab.removeAttribute("pending");
    aTab.removeAttribute("discarded");

    if (previousState == TAB_STATE_RESTORING) {
      if (this._tabsRestoringCount) {
        this._tabsRestoringCount--;
      }
    } else if (previousState == TAB_STATE_NEEDS_RESTORE) {
      // Make sure that the tab is removed from the list of tabs to restore.
      // Again, this is normally done in restoreTabContent, but that isn't being called
      // for this tab.
      TabRestoreQueue.remove(aTab);
    }
  },

  _resetTabRestoringState(tab) {
    let browser = tab.linkedBrowser;

    if (!TAB_STATE_FOR_BROWSER.has(browser)) {
      console.error("Given tab is not restoring.");
      return;
    }

    this._resetLocalTabRestoringState(tab);
  },

  /**
   * Each fresh tab starts out with epoch=0. This function can be used to
   * start a next epoch by incrementing the current value. It will enables us
   * to ignore stale messages sent from previous epochs. The function returns
   * the new epoch ID for the given |browser|.
   */
  startNextEpoch(permanentKey) {
    let next = this.getCurrentEpoch(permanentKey) + 1;
    this._browserEpochs.set(permanentKey, next);
    return next;
  },

  /**
   * Returns the current epoch for the given <browser>. If we haven't assigned
   * a new epoch this will default to zero for new tabs.
   */
  getCurrentEpoch(permanentKey) {
    return this._browserEpochs.get(permanentKey) || 0;
  },

  /**
   * Each time a <browser> element is restored, we increment its "epoch". To
   * check if a message from content-sessionStore.js is out of date, we can
   * compare the epoch received with the message to the <browser> element's
   * epoch. This function does that, and returns true if |epoch| is up-to-date
   * with respect to |browser|.
   */
  isCurrentEpoch(permanentKey, epoch) {
    return this.getCurrentEpoch(permanentKey) == epoch;
  },

  /**
   * Resets the epoch for a given <browser>. We need to this every time we
   * receive a hint that a new docShell has been loaded into the browser as
   * the frame script starts out with epoch=0.
   */
  resetEpoch(permanentKey, frameLoader = null) {
    this._browserEpochs.delete(permanentKey);
    if (frameLoader) {
      frameLoader.requestEpochUpdate(0);
    }
  },

  /**
   * Countdown for a given duration, skipping beats if the computer is too busy,
   * sleeping or otherwise unavailable.
   *
   * @param {number} delay An approximate delay to wait in milliseconds (rounded
   * up to the closest second).
   *
   * @return Promise
   */
  looseTimer(delay) {
    let DELAY_BEAT = 1000;
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    let beats = Math.ceil(delay / DELAY_BEAT);
    let deferred = Promise.withResolvers();
    timer.initWithCallback(
      function () {
        if (beats <= 0) {
          deferred.resolve();
        }
        --beats;
      },
      DELAY_BEAT,
      Ci.nsITimer.TYPE_REPEATING_PRECISE_CAN_SKIP
    );
    // Ensure that the timer is both canceled once we are done with it
    // and not garbage-collected until then.
    deferred.promise.then(
      () => timer.cancel(),
      () => timer.cancel()
    );
    return deferred;
  },

  _waitForStateStop(browser, expectedURL = null) {
    const deferred = Promise.withResolvers();

    const listener = {
      unregister(reject = true) {
        if (reject) {
          deferred.reject();
        }

        SessionStoreInternal._restoreListeners.delete(browser.permanentKey);

        try {
          browser.removeProgressListener(
            this,
            Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
          );
        } catch {} // May have already gotten rid of the browser's webProgress.
      },

      onStateChange(webProgress, request, stateFlags) {
        if (
          webProgress.isTopLevel &&
          stateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW &&
          stateFlags & Ci.nsIWebProgressListener.STATE_STOP
        ) {
          // FIXME: We sometimes see spurious STATE_STOP events for about:blank
          // loads, so we have to account for that here.
          let aboutBlankOK = !expectedURL || expectedURL === "about:blank";
          let url = request.QueryInterface(Ci.nsIChannel).originalURI.spec;
          if (url !== "about:blank" || aboutBlankOK) {
            this.unregister(false);
            deferred.resolve();
          }
        }
      },

      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),
    };

    this._restoreListeners.get(browser.permanentKey)?.unregister();
    this._restoreListeners.set(browser.permanentKey, listener);

    browser.addProgressListener(
      listener,
      Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
    );

    return deferred.promise;
  },

  _listenForNavigations(browser, callbacks) {
    const listener = {
      unregister() {
        browser.browsingContext?.sessionHistory?.removeSHistoryListener(this);

        try {
          browser.removeProgressListener(
            this,
            Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
          );
        } catch {} // May have already gotten rid of the browser's webProgress.

        SessionStoreInternal._restoreListeners.delete(browser.permanentKey);
      },

      OnHistoryReload() {
        this.unregister();
        return callbacks.onHistoryReload();
      },

      // TODO(kashav): ContentRestore.sys.mjs handles OnHistoryNewEntry
      // separately, so we should eventually support that here as well.
      OnHistoryNewEntry() {},
      OnHistoryGotoIndex() {},
      OnHistoryPurge() {},
      OnHistoryReplaceEntry() {},

      onStateChange(webProgress, request, stateFlags) {
        if (
          webProgress.isTopLevel &&
          stateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW &&
          stateFlags & Ci.nsIWebProgressListener.STATE_START
        ) {
          this.unregister();
          callbacks.onStartRequest();
        }
      },

      QueryInterface: ChromeUtils.generateQI([
        "nsISHistoryListener",
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),
    };

    this._restoreListeners.get(browser.permanentKey)?.unregister();
    this._restoreListeners.set(browser.permanentKey, listener);

    browser.browsingContext?.sessionHistory?.addSHistoryListener(listener);

    browser.addProgressListener(
      listener,
      Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
    );
  },

  /**
   * This mirrors ContentRestore.restoreHistory() for parent process session
   * history restores.
   */
  _restoreHistory(browser, data) {
    this._tabStateToRestore.set(browser.permanentKey, data);

    // In case about:blank isn't done yet.
    // XXX(kashav): Does this actually accomplish anything? Can we remove?
    browser.stop();

    lazy.SessionHistory.restoreFromParent(
      browser.browsingContext.sessionHistory,
      data.tabData
    );

    let url = data.tabData?.entries[data.tabData.index - 1]?.url;
    let disallow = data.tabData?.disallow;

    let promise = SessionStoreUtils.restoreDocShellState(
      browser.browsingContext,
      url,
      disallow
    );
    this._tabStateRestorePromises.set(browser.permanentKey, promise);

    const onResolve = () => {
      if (TAB_STATE_FOR_BROWSER.get(browser) !== TAB_STATE_RESTORING) {
        this._listenForNavigations(browser, {
          // The history entry was reloaded before we began restoring tab
          // content, just proceed as we would normally.
          onHistoryReload: () => {
            this._restoreTabContent(browser);
            return false;
          },

          // Some foreign code, like an extension, loaded a new URI on the
          // browser. We no longer want to restore saved tab data, but may
          // still have browser state that needs to be restored.
          onStartRequest: () => {
            this._tabStateToRestore.delete(browser.permanentKey);
            this._restoreTabContent(browser);
          },
        });
      }

      this._tabStateRestorePromises.delete(browser.permanentKey);

      this._restoreHistoryComplete(browser);
    };

    promise.then(onResolve).catch(() => {});
  },

  /**
   * Either load the saved typed value or restore the active history entry.
   * If neither is possible, just load an empty document.
   */
  _restoreTabEntry(browser, tabData) {
    let haveUserTypedValue = tabData.userTypedValue && tabData.userTypedClear;
    // First take care of the common case where we load the history entry.
    if (!haveUserTypedValue && tabData.entries.length) {
      return SessionStoreUtils.initializeRestore(
        browser.browsingContext,
        lazy.SessionStoreHelper.buildRestoreData(
          tabData.formdata,
          tabData.scroll
        )
      );
    }
    // Here, we need to load user data or about:blank instead.
    // As it's user-typed (or blank), it gets system triggering principal:
    let triggeringPrincipal =
      Services.scriptSecurityManager.getSystemPrincipal();
    // Bypass all the fixup goop for about:blank:
    if (!haveUserTypedValue) {
      let blankPromise = this._waitForStateStop(browser, "about:blank");
      browser.browsingContext.loadURI(lazy.blankURI, {
        triggeringPrincipal,
        loadFlags: Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_HISTORY,
      });
      return blankPromise;
    }

    // We have a user typed value, load that with fixup:
    let loadPromise = this._waitForStateStop(browser, tabData.userTypedValue);
    browser.browsingContext.fixupAndLoadURIString(tabData.userTypedValue, {
      loadFlags: Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP,
      triggeringPrincipal,
    });

    return loadPromise;
  },

  /**
   * This mirrors ContentRestore.restoreTabContent() for parent process session
   * history restores.
   */
  _restoreTabContent(browser, options = {}) {
    this._restoreListeners.get(browser.permanentKey)?.unregister();

    this._restoreTabContentStarted(browser, options);

    let state = this._tabStateToRestore.get(browser.permanentKey);
    this._tabStateToRestore.delete(browser.permanentKey);

    let promises = [this._tabStateRestorePromises.get(browser.permanentKey)];

    if (state) {
      promises.push(this._restoreTabEntry(browser, state.tabData));
    } else {
      // The browser started another load, so we decided to not restore
      // saved tab data. We should still wait for that new load to finish
      // before proceeding.
      promises.push(this._waitForStateStop(browser));
    }

    Promise.allSettled(promises).then(() => {
      this._restoreTabContentComplete(browser, options);
    });
  },

  _sendRestoreTabContent(browser, options) {
    this._restoreTabContent(browser, options);
  },

  _restoreHistoryComplete(browser) {
    let win = browser.ownerGlobal;
    let tab = win?.gBrowser.getTabForBrowser(browser);
    if (!tab) {
      return;
    }

    // Notify the tabbrowser that the tab chrome has been restored.
    let tabData = lazy.TabState.collect(tab, TAB_CUSTOM_VALUES.get(tab));

    // Update tab label and icon again after the tab history was updated.
    this.updateTabLabelAndIcon(tab, tabData);

    let event = win.document.createEvent("Events");
    event.initEvent("SSTabRestoring", true, false);
    tab.dispatchEvent(event);
  },

  _restoreTabContentStarted(browser, data) {
    let win = browser.ownerGlobal;
    let tab = win?.gBrowser.getTabForBrowser(browser);
    if (!tab) {
      return;
    }

    let initiatedBySessionStore =
      TAB_STATE_FOR_BROWSER.get(browser) != TAB_STATE_NEEDS_RESTORE;
    let isNavigateAndRestore =
      data.reason == RESTORE_TAB_CONTENT_REASON.NAVIGATE_AND_RESTORE;

    // We need to be careful when restoring the urlbar's search mode because
    // we race a call to gURLBar.setURI due to the location change.  setURI
    // will exit search mode and set gURLBar.value to the restored URL,
    // clobbering any search mode and userTypedValue we restore here.  If
    // this is a typical restore -- restoring on startup or restoring a
    // closed tab for example -- then we need to restore search mode after
    // that setURI call, and so we wait until restoreTabContentComplete, at
    // which point setURI will have been called.  If this is not a typical
    // restore -- it was not initiated by session store or it's due to a
    // remoteness change -- then we do not want to restore search mode at
    // all, and so we remove it from the tab state cache.  In particular, if
    // the restore is due to a remoteness change, then the user is loading a
    // new URL and the current search mode should not be carried over to it.
    let cacheState = lazy.TabStateCache.get(browser.permanentKey);
    if (cacheState.searchMode) {
      if (!initiatedBySessionStore || isNavigateAndRestore) {
        lazy.TabStateCache.update(browser.permanentKey, {
          searchMode: null,
          userTypedValue: null,
        });
      }
      return;
    }

    if (!initiatedBySessionStore) {
      // If a load not initiated by sessionstore was started in a
      // previously pending tab. Mark the tab as no longer pending.
      this.markTabAsRestoring(tab);
    } else if (!isNavigateAndRestore) {
      // If the user was typing into the URL bar when we crashed, but hadn't hit
      // enter yet, then we just need to write that value to the URL bar without
      // loading anything. This must happen after the load, as the load will clear
      // userTypedValue.
      //
      // Note that we only want to do that if we're restoring state for reasons
      // _other_ than a navigateAndRestore remoteness-flip, as such a flip
      // implies that the user was navigating.
      let tabData = lazy.TabState.collect(tab, TAB_CUSTOM_VALUES.get(tab));
      if (
        tabData.userTypedValue &&
        !tabData.userTypedClear &&
        !browser.userTypedValue
      ) {
        browser.userTypedValue = tabData.userTypedValue;
        if (tab.selected) {
          win.gURLBar.setURI();
        }
      }

      // Remove state we don't need any longer.
      lazy.TabStateCache.update(browser.permanentKey, {
        userTypedValue: null,
        userTypedClear: null,
      });
    }
  },

  _restoreTabContentComplete(browser, data) {
    let win = browser.ownerGlobal;
    let tab = browser.ownerGlobal?.gBrowser.getTabForBrowser(browser);
    if (!tab) {
      return;
    }
    // Restore search mode and its search string in userTypedValue, if
    // appropriate.
    let cacheState = lazy.TabStateCache.get(browser.permanentKey);
    if (cacheState.searchMode) {
      win.gURLBar.setSearchMode(cacheState.searchMode, browser);
      browser.userTypedValue = cacheState.userTypedValue;
      if (tab.selected) {
        win.gURLBar.setURI();
      }
      lazy.TabStateCache.update(browser.permanentKey, {
        searchMode: null,
        userTypedValue: null,
      });
    }

    // This callback is used exclusively by tests that want to
    // monitor the progress of network loads.
    if (gDebuggingEnabled) {
      Services.obs.notifyObservers(browser, NOTIFY_TAB_RESTORED);
    }

    SessionStoreInternal._resetLocalTabRestoringState(tab);
    SessionStoreInternal.restoreNextTab();

    this._sendTabRestoredNotification(tab, data.isRemotenessUpdate);

    Services.obs.notifyObservers(null, "sessionstore-one-or-no-tab-restored");
  },

  /**
   * Send the "SessionStore:restoreHistory" message to content, triggering a
   * content restore. This method is intended to be used internally by
   * SessionStore, as it also ensures that permissions are avaliable in the
   * content process before triggering the history restore in the content
   * process.
   *
   * @param browser The browser to transmit the permissions for
   * @param options The options data to send to content.
   */
  _sendRestoreHistory(browser, options) {
    if (options.tabData.storage) {
      SessionStoreUtils.restoreSessionStorageFromParent(
        browser.browsingContext,
        options.tabData.storage
      );
      delete options.tabData.storage;
    }

    this._restoreHistory(browser, options);

    if (browser && browser.frameLoader) {
      browser.frameLoader.requestEpochUpdate(options.epoch);
    }
  },

  /**
   * @param {MozTabbrowserTabGroup} tabGroup
   */
  addSavedTabGroup(tabGroup) {
    if (PrivateBrowsingUtils.isWindowPrivate(tabGroup.ownerGlobal)) {
      throw new Error("Refusing to save tab group from private window");
    }

    let tabGroupState = lazy.TabGroupState.savedInOpenWindow(
      tabGroup,
      tabGroup.ownerGlobal.__SSi
    );
    tabGroupState.tabs = this._collectClosedTabsForTabGroup(
      tabGroup.tabs,
      tabGroup.ownerGlobal
    );
    this._recordSavedTabGroupState(tabGroupState);
  },

  /**
   * @param {SavedTabGroupStateData} savedTabGroupState
   * @returns {void}
   */
  _recordSavedTabGroupState(savedTabGroupState) {
    if (
      !savedTabGroupState.tabs.length ||
      this.getSavedTabGroup(savedTabGroupState.id)
    ) {
      return;
    }
    this._savedGroups.push(savedTabGroupState);
    this._notifyOfSavedTabGroupsChange();
  },

  /**
   * @param {string} tabGroupId
   * @returns {SavedTabGroupStateData|undefined}
   */
  getSavedTabGroup(tabGroupId) {
    return this._savedGroups.find(
      savedTabGroup => savedTabGroup.id == tabGroupId
    );
  },

  /**
   * Returns all tab groups that were saved in this session.
   * @returns {SavedTabGroupStateData[]}
   */
  getSavedTabGroups() {
    return Cu.cloneInto(this._savedGroups, {});
  },

  /**
   * @param {Window|{sourceWindowId: string}|{sourceClosedId: number}} source
   * @param {string} tabGroupId
   * @returns {ClosedTabGroupStateData|undefined}
   */
  getClosedTabGroup(source, tabGroupId) {
    let winData = this._resolveClosedDataSource(source);
    return winData?.closedGroups.find(
      closedGroup => closedGroup.id == tabGroupId
    );
  },

  /**
   * @param {Window|Object} source
   * @param {string} tabGroupId
   * @param {Window} [targetWindow]
   * @returns {MozTabbrowserTabGroup}
   */
  undoCloseTabGroup(source, tabGroupId, targetWindow) {
    const sourceWinData = this._resolveClosedDataSource(source);
    const isPrivateSource = Boolean(sourceWinData.isPrivate);
    if (targetWindow && !targetWindow.__SSi) {
      throw Components.Exception(
        "Target window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    } else if (!targetWindow) {
      targetWindow = this._getTopWindow(isPrivateSource);
    }
    if (
      isPrivateSource !== PrivateBrowsingUtils.isWindowPrivate(targetWindow)
    ) {
      throw Components.Exception(
        "Target window doesn't have the same privateness as the source window",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let tabGroupData = this.getClosedTabGroup(source, tabGroupId);
    if (!tabGroupData) {
      throw Components.Exception(
        "Tab group not found in source",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let group = this._createTabsForSavedOrClosedTabGroup(
      tabGroupData,
      targetWindow
    );
    this.forgetClosedTabGroup(source, tabGroupId);
    sourceWinData.lastClosedTabGroupId = null;

    Glean.tabgroup.groupInteractions.open_recent.add(1);

    group.select();
    return group;
  },

  /**
   * @param {string} tabGroupId
   * @param {Window} [targetWindow]
   * @returns {MozTabbrowserTabGroup}
   */
  openSavedTabGroup(tabGroupId, targetWindow) {
    if (!targetWindow) {
      targetWindow = this._getTopWindow();
    }
    if (!targetWindow.__SSi) {
      throw Components.Exception(
        "Target window is not tracked",
        Cr.NS_ERROR_INVALID_ARG
      );
    }
    if (PrivateBrowsingUtils.isWindowPrivate(targetWindow)) {
      throw Components.Exception(
        "Cannot open a saved tab group in a private window",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let tabGroupData = this.getSavedTabGroup(tabGroupId);
    if (!tabGroupData) {
      throw Components.Exception(
        "No saved tab group with specified id",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    // If this saved tab group is present in a closed window, then we need to
    // remove references to this saved tab group from that closed window. The
    // result should be as if the saved tab group "moved" from the closed window
    // into the `targetWindow`.
    if (tabGroupData.windowClosedId) {
      let closedWinData = this.getClosedWindowDataByClosedId(
        tabGroupData.windowClosedId
      );
      if (closedWinData) {
        this._removeSavedTabGroupFromClosedWindow(
          closedWinData,
          tabGroupData.id
        );
      }
    }

    let group = this._createTabsForSavedOrClosedTabGroup(
      tabGroupData,
      targetWindow
    );
    this.forgetSavedTabGroup(tabGroupId);

    group.select();
    return group;
  },

  /**
   * @param {ClosedTabGroupStateData|SavedTabGroupStateData} tabGroupData
   * @param {Window} targetWindow
   * @returns {MozTabbrowserTabGroup}
   */
  _createTabsForSavedOrClosedTabGroup(tabGroupData, targetWindow) {
    let tabDataList = tabGroupData.tabs.map(tab => tab.state);
    let tabs = targetWindow.gBrowser.createTabsForSessionRestore(
      true,
      0, // TODO Bug 1933113 - Save tab group position and selected tab with saved tab group data
      tabDataList,
      [tabGroupData]
    );

    this.restoreTabs(targetWindow, tabs, tabDataList, 0);
    return tabs[0].group;
  },

  /**
   * Remove tab groups from the closedGroups list that have no tabs associated
   * with them.
   *
   * This can sometimes happen because tab groups are immediately
   * added to closedGroups on closing, before the complete history of the tabs
   * within the group have been processed. If it is later determined that none
   * of the tabs in the group were "worth saving", the group will be empty.
   * This can also happen if a user "undoes" the last closed tab in a closed tab
   * group.
   *
   * See: bug1933966, bug1933485
   *
   * @param {WindowStateData} winData
   */
  _cleanupOrphanedClosedGroups(winData) {
    if (!winData.closedGroups) {
      return;
    }
    for (let index = winData.closedGroups.length - 1; index >= 0; index--) {
      if (winData.closedGroups[index].tabs.length === 0) {
        winData.closedGroups.splice(index, 1);
        this._closedObjectsChanged = true;
      }
    }
  },

  /**
   * @param {WindowStateData} closedWinData
   * @param {string} tabGroupId
   * @returns {void} modifies the data in argument `closedWinData`
   */
  _removeSavedTabGroupFromClosedWindow(closedWinData, tabGroupId) {
    removeWhere(closedWinData.groups, tabGroup => tabGroup.id == tabGroupId);
    removeWhere(closedWinData.tabs, tab => tab.groupId == tabGroupId);
    this._closedObjectsChanged = true;
  },
};

/**
 * Priority queue that keeps track of a list of tabs to restore and returns
 * the tab we should restore next, based on priority rules. We decide between
 * pinned, visible and hidden tabs in that and FIFO order. Hidden tabs are only
 * restored with restore_hidden_tabs=true.
 */
var TabRestoreQueue = {
  // The separate buckets used to store tabs.
  tabs: { priority: [], visible: [], hidden: [] },

  // Preferences used by the TabRestoreQueue to determine which tabs
  // are restored automatically and which tabs will be on-demand.
  prefs: {
    // Lazy getter that returns whether tabs are restored on demand.
    get restoreOnDemand() {
      let updateValue = () => {
        let value = Services.prefs.getBoolPref(PREF);
        let definition = { value, configurable: true };
        Object.defineProperty(this, "restoreOnDemand", definition);
        return value;
      };

      const PREF = "browser.sessionstore.restore_on_demand";
      Services.prefs.addObserver(PREF, updateValue);
      return updateValue();
    },

    // Lazy getter that returns whether pinned tabs are restored on demand.
    get restorePinnedTabsOnDemand() {
      let updateValue = () => {
        let value = Services.prefs.getBoolPref(PREF);
        let definition = { value, configurable: true };
        Object.defineProperty(this, "restorePinnedTabsOnDemand", definition);
        return value;
      };

      const PREF = "browser.sessionstore.restore_pinned_tabs_on_demand";
      Services.prefs.addObserver(PREF, updateValue);
      return updateValue();
    },

    // Lazy getter that returns whether we should restore hidden tabs.
    get restoreHiddenTabs() {
      let updateValue = () => {
        let value = Services.prefs.getBoolPref(PREF);
        let definition = { value, configurable: true };
        Object.defineProperty(this, "restoreHiddenTabs", definition);
        return value;
      };

      const PREF = "browser.sessionstore.restore_hidden_tabs";
      Services.prefs.addObserver(PREF, updateValue);
      return updateValue();
    },
  },

  // Resets the queue and removes all tabs.
  reset() {
    this.tabs = { priority: [], visible: [], hidden: [] };
  },

  // Adds a tab to the queue and determines its priority bucket.
  add(tab) {
    let { priority, hidden, visible } = this.tabs;

    if (tab.pinned) {
      priority.push(tab);
    } else if (tab.hidden) {
      hidden.push(tab);
    } else {
      visible.push(tab);
    }
  },

  // Removes a given tab from the queue, if it's in there.
  remove(tab) {
    let { priority, hidden, visible } = this.tabs;

    // We'll always check priority first since we don't
    // have an indicator if a tab will be there or not.
    let set = priority;
    let index = set.indexOf(tab);

    if (index == -1) {
      set = tab.hidden ? hidden : visible;
      index = set.indexOf(tab);
    }

    if (index > -1) {
      set.splice(index, 1);
    }
  },

  // Returns and removes the tab with the highest priority.
  shift() {
    let set;
    let { priority, hidden, visible } = this.tabs;

    let { restoreOnDemand, restorePinnedTabsOnDemand } = this.prefs;
    let restorePinned = !(restoreOnDemand && restorePinnedTabsOnDemand);
    if (restorePinned && priority.length) {
      set = priority;
    } else if (!restoreOnDemand) {
      if (visible.length) {
        set = visible;
      } else if (this.prefs.restoreHiddenTabs && hidden.length) {
        set = hidden;
      }
    }

    return set && set.shift();
  },

  // Moves a given tab from the 'hidden' to the 'visible' bucket.
  hiddenToVisible(tab) {
    let { hidden, visible } = this.tabs;
    let index = hidden.indexOf(tab);

    if (index > -1) {
      hidden.splice(index, 1);
      visible.push(tab);
    }
  },

  // Moves a given tab from the 'visible' to the 'hidden' bucket.
  visibleToHidden(tab) {
    let { visible, hidden } = this.tabs;
    let index = visible.indexOf(tab);

    if (index > -1) {
      visible.splice(index, 1);
      hidden.push(tab);
    }
  },

  /**
   * Returns true if the passed tab is in one of the sets that we're
   * restoring content in automatically.
   *
   * @param tab (<xul:tab>)
   *        The tab to check
   * @returns bool
   */
  willRestoreSoon(tab) {
    let { priority, hidden, visible } = this.tabs;
    let { restoreOnDemand, restorePinnedTabsOnDemand, restoreHiddenTabs } =
      this.prefs;
    let restorePinned = !(restoreOnDemand && restorePinnedTabsOnDemand);
    let candidateSet = [];

    if (restorePinned && priority.length) {
      candidateSet.push(...priority);
    }

    if (!restoreOnDemand) {
      if (visible.length) {
        candidateSet.push(...visible);
      }

      if (restoreHiddenTabs && hidden.length) {
        candidateSet.push(...hidden);
      }
    }

    return candidateSet.indexOf(tab) > -1;
  },
};

// A map storing a closed window's state data until it goes aways (is GC'ed).
// This ensures that API clients can still read (but not write) states of
// windows they still hold a reference to but we don't.
var DyingWindowCache = {
  _data: new WeakMap(),

  has(window) {
    return this._data.has(window);
  },

  get(window) {
    return this._data.get(window);
  },

  set(window, data) {
    this._data.set(window, data);
  },

  remove(window) {
    this._data.delete(window);
  },
};

// A weak set of dirty windows. We use it to determine which windows we need to
// recollect data for when getCurrentState() is called.
var DirtyWindows = {
  _data: new WeakMap(),

  has(window) {
    return this._data.has(window);
  },

  add(window) {
    return this._data.set(window, true);
  },

  remove(window) {
    this._data.delete(window);
  },

  clear(_window) {
    this._data = new WeakMap();
  },
};

// The state from the previous session (after restoring pinned tabs). This
// state is persisted and passed through to the next session during an app
// restart to make the third party add-on warning not trash the deferred
// session
var LastSession = {
  _state: null,

  get canRestore() {
    return !!this._state;
  },

  getState() {
    return this._state;
  },

  setState(state) {
    this._state = state;
  },

  clear(silent = false) {
    if (this._state) {
      this._state = null;
      if (!silent) {
        Services.obs.notifyObservers(null, NOTIFY_LAST_SESSION_CLEARED);
      }
    }
  },
};

/**
 * @template T
 * @param {T[]} array
 * @param {function(T):boolean} predicate
 */
function removeWhere(array, predicate) {
  for (let i = array.length - 1; i >= 0; i--) {
    if (predicate(array[i])) {
      array.splice(i, 1);
    }
  }
}

// Exposed for tests
export const _LastSession = LastSession;
