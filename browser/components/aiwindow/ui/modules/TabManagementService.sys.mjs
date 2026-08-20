/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SessionStore } from "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "TabManagementService",
  })
);

/**
 * Service for managing browser tabs from AI Window UI components.
 *
 * This service closes tabs using gBrowser.removeTab(), allowing Firefox's
 * native SessionStore machinery to keep the actual closed-tab restore state.
 *
 * The service only stores lightweight operation metadata so the AI Window can
 * target a specific close operation when the user clicks "Undo".
 */
export class TabManagementService {
  /**
   * Available colors for tab groups.
   */
  static TAB_GROUP_COLORS = [
    "blue",
    "purple",
    "cyan",
    "orange",
    "yellow",
    "pink",
    "green",
    "gray",
    "red",
  ];

  /**
   * Constructor allows dependency injection for testing.
   *
   * @param {object} sessionStore - Optional SessionStore instance for testing
   */
  constructor(sessionStore = null) {
    this.#sessionStore = sessionStore || SessionStore;
  }

  /**
   * SessionStore instance (real or mock)
   */
  #sessionStore;
  /**
   * Map of operation ID to close-operation metadata.
   *
   * Structure:
   * Map<string, {
   *   closedTabs: Array<{
   *     // Tab identification
   *     tabId: string | null,        // Custom tab ID if assigned
   *     url: string | null,           // Tab URL (primary matching key)
   *     title: string,                // Tab title for display
   *     userContextId: number,        // Container ID (0 = default, >0 = container)
   *
   *     // Operation metadata
   *     operationTimestamp: number    // When this close operation occurred
   *   }>,
   *   timestamp: number,              // When operation was stored
   *   windowRef: WeakRef<Window>|null // WeakRef to the window tabs closed in
   * }>
   *
   * Note: This only stores lightweight metadata for matching tabs in SessionStore.
   * The actual restore data (history, scroll position, form data, etc.) remains
   * in SessionStore's closed-tab list.
   */
  #recentCloseOperations = new Map();

  /**
   * Maximum number of close operations to remember for undo.
   */
  #MAX_STORED_OPERATIONS = 10;

  /**
   * Counter for generating unique operation IDs.
   */
  #operationCounter = 0;

  /**
   * Restores tabs closed by a specific operation.
   *
   * @param {object} options
   * @param {string} options.operationId - ID returned from closeTabs()
   * @returns {Promise<object>} Restore summary
   */
  async restoreTabs({ operationId }) {
    const operation = this.#recentCloseOperations.get(operationId);

    if (!operation) {
      lazy.console.warn(`No stored tab-close operation found: ${operationId}`);

      return {
        restoredTabs: [],
        restoredCount: 0,
        requestedCount: 0,
        failedTabs: [],
      };
    }

    // Resolve the owning window from the record
    const window = operation.windowRef?.get();
    if (!window?.gBrowser) {
      lazy.console.warn(
        `The owning window is not available for tab-close operation: ${operationId}`
      );
      return {
        restoredTabs: [],
        restoredCount: 0,
        requestedCount: operation.closedTabs.length,
        failedTabs: operation.closedTabs.map(tab => ({
          tab,
          reason: "owning-window-closed",
        })),
      };
    }

    const restoredTabs = [];
    const failedTabs = [];

    // Remember the currently selected tab to switch back to it after restoration
    const originalSelectedTab = window.gBrowser.selectedTab;

    for (const closedOperationTab of operation.closedTabs) {
      try {
        const closedTabIndex = this.#findClosedTabIndexForOperationTab(
          window,
          closedOperationTab
        );

        if (closedTabIndex == null) {
          failedTabs.push({
            tab: closedOperationTab,
            reason: "matching-closed-tab-not-found",
          });
          continue;
        }

        const restoredTab = this.#sessionStore.undoCloseTab(
          window,
          closedTabIndex,
          window
        );

        if (restoredTab) {
          restoredTabs.push(restoredTab);
        } else {
          failedTabs.push({
            tab: closedOperationTab,
            reason: "undo-returned-null",
          });
        }
      } catch (error) {
        lazy.console.error(
          `Failed to restore tab ${closedOperationTab.url}:`,
          error
        );
        failedTabs.push({
          tab: closedOperationTab,
          reason: "exception",
          message: error.message,
        });
      }
    }

    // Switch back to the originally selected tab to keep restorations in background
    if (
      originalSelectedTab &&
      window.gBrowser.tabs.includes(originalSelectedTab)
    ) {
      window.gBrowser.selectedTab = originalSelectedTab;
    }

    // Only delete the operation if all tabs were successfully restored
    if (!failedTabs.length) {
      this.#recentCloseOperations.delete(operationId);
    }

    return {
      restoredTabs,
      restoredCount: restoredTabs.length,
      requestedCount: operation.closedTabs.length,
      failedTabs,
    };
  }

  /**
   * Stores metadata for a close operation.
   *
   * The actual restore data is owned by SessionStore. This metadata is only
   * used to find the matching closed-tab entries later.
   *
   * @param {object} options
   * @param {Array<object>} options.closedTabs
   * @param {Window} [options.window] - Window the tabs were closed in
   * @returns {string|null}
   */
  storeClosedTabsForUndo({ closedTabs, window }) {
    if (!closedTabs?.length) {
      return null;
    }

    this.#operationCounter++;
    const operationId = `tab-close-${this.#operationCounter}`;

    if (this.#recentCloseOperations.size >= this.#MAX_STORED_OPERATIONS) {
      const oldestId = this.#recentCloseOperations.keys().next().value;
      this.#recentCloseOperations.delete(oldestId);
    }

    this.#recentCloseOperations.set(operationId, {
      closedTabs,
      timestamp: Date.now(),
      windowRef: window ? Cu.getWeakReference(window) : null,
    });

    return operationId;
  }

  /**
   * Gets stored metadata for a close operation.
   *
   * @param {string} operationId
   * @returns {object|null}
   */
  getStoredTabsForUndo(operationId) {
    return this.#recentCloseOperations.get(operationId) || null;
  }

  /**
   * Creates a tab group from the provided tabs.
   *
   * @param {object} options
   * @param {Array<Tab>} options.tabs - Array of tab objects to group
   * @param {Window} options.window - Browser window containing the tabs
   * @param {string} [options.label] - Label for the tab group
   * @param {string} [options.color] - Color for the tab group
   * @param {string} [options.id] - Optional ID for the tab group
   * @returns {Promise<{
   *   success: boolean,
   *   group: {
   *     id: string,
   *     label: string,
   *     color: string,
   *     tabCount: number
   *   } | null,
   *   failedTabs: Array<{
   *     tab: Tab,
   *     reason: string
   *   }>,
   *   error?: string
   * }>} Creation summary with group details, success status, and any failed tabs
   */
  async createTabGroup({
    tabs,
    window,
    label = "Tab Group",
    color = null,
    id = null,
  }) {
    if (!tabs?.length) {
      lazy.console.warn("No tabs to group");

      return {
        success: false,
        group: null,
        failedTabs: [],
        error: "No tabs provided",
      };
    }

    if (!window?.gBrowser) {
      return {
        success: false,
        group: null,
        failedTabs: [],
        error: "Invalid browser window provided",
      };
    }

    const { validTabs, failedTabs } = this.#validateTabsForGrouping(
      tabs,
      window
    );

    if (!validTabs.length) {
      return {
        success: false,
        group: null,
        failedTabs,
        error: "No valid tabs to group",
      };
    }

    const groupColor = color || this.#getNextUnusedColor(window);

    try {
      const group = window.gBrowser.addTabGroup(validTabs, {
        id,
        color: groupColor,
        label,
        telemetryUserCreateSource: "ai_window",
      });

      if (!group) {
        return {
          success: false,
          group: null,
          failedTabs,
          error: "Failed to create tab group",
        };
      }

      return {
        success: true,
        group: {
          id: group.id,
          label: group.label,
          color: group.color,
          tabCount: group.tabs.length,
        },
        failedTabs,
      };
    } catch (error) {
      lazy.console.error("Failed to create tab group:", error);
      return {
        success: false,
        group: null,
        failedTabs,
        error: error.message,
      };
    }
  }

  /**
   * Opens a list of URLs as new background tabs in the given window.
   *
   * Always opens a fresh tab per URL - does not check whether a matching
   * tab is already open. See findOpenTab() for that.
   *
   * @param {object} options
   * @param {Array<string>} options.urls - URLs to open as new tabs
   * @param {Window} options.window - Browser window to open the tabs in
   * @returns {{
   *   openedTabs: Array<Tab>,
   *   failedUrls: Array<{url: string, reason: string}>
   * }} Result with the newly-opened tabs and any that failed to open
   */
  openTabs({ urls, window }) {
    if (!urls?.length) {
      lazy.console.warn("No URLs to open");
      return { openedTabs: [], failedUrls: [] };
    }

    if (!window?.gBrowser) {
      throw new Error("Invalid browser window provided");
    }

    const openedTabs = [];
    const failedUrls = [];
    const triggeringPrincipal =
      Services.scriptSecurityManager.getSystemPrincipal();

    for (const url of urls) {
      try {
        openedTabs.push(
          window.gBrowser.addTab(url, {
            inBackground: true,
            triggeringPrincipal,
          })
        );
      } catch (error) {
        lazy.console.error(`Failed to open tab for ${url}:`, error);
        failedUrls.push({ url, reason: error.message });
      }
    }

    return { openedTabs, failedUrls };
  }

  /**
   * Switches to an already-open tab.
   *
   * @param {object} options
   * @param {Tab} options.tab - Tab to switch to
   * @param {Window} options.window - Browser window containing the tab
   */
  switchToTab({ tab, window }) {
    if (!tab || !window?.gBrowser) {
      lazy.console.warn("Invalid tab or window provided to switchToTab");
      return;
    }
    window.gBrowser.selectedTab = tab;
  }

  /**
   * Finds a tab already open in the given window whose URL exactly matches.
   *
   * @param {object} options
   * @param {string} options.url - URL to match
   * @param {Window} options.window - Browser window to search
   * @param {Set<Tab>} [options.excludeTabs] - Tabs to skip, e.g. ones
   *   already claimed by an earlier match in the same batch
   * @returns {Tab|null} The matching tab, or null if none found
   */
  findOpenTab({ url, window, excludeTabs = null }) {
    if (!window?.gBrowser) {
      return null;
    }

    let normalizedUrl;
    try {
      normalizedUrl = Services.io.newURI(url).spec;
    } catch (e) {
      return null;
    }

    return (
      window.gBrowser.tabs.find(
        tab =>
          !tab.closing &&
          !excludeTabs?.has(tab) &&
          tab.linkedBrowser?.currentURI?.spec === normalizedUrl
      ) ?? null
    );
  }

  /**
   * Resolves a list of tabs against tabs already open in the given window,
   * opening a fresh tab via openTabs() only for the URLs that don't match
   * one already open. Matching is exact-URL and scoped to this window only
   * - it does not search other windows.
   *
   * @param {object} options
   * @param {Array<{url: string}>} options.tabs - Tabs to resolve, by URL
   * @param {Window} options.window - Browser window to search/open tabs in
   * @returns {Promise<{
   *   resolvedTabs: Array<Tab>,
   *   mergedCount: number,
   *   failedUrls: Array<{url: string, reason: string}>
   * }>} Resolved tabs in the original order, how many were merged rather
   *   than opened, and any URLs that failed to open
   */
  async resolveOrOpenTabs({ tabs, window }) {
    if (!tabs?.length) {
      lazy.console.warn("No tabs to resolve");
      return { resolvedTabs: [], mergedCount: 0, failedUrls: [] };
    }

    if (!window?.gBrowser) {
      throw new Error("Invalid browser window provided");
    }

    const claimedTabs = new Set();
    const resolvedTabs = [];
    const failedUrls = [];
    let mergedCount = 0;

    for (const { url } of tabs) {
      const existingTab = this.findOpenTab({
        url,
        window,
        excludeTabs: claimedTabs,
      });

      // Pinned/already-grouped tabs would be rejected later by
      // createTabGroup's own validation, so a match here can't be treated
      // as merged - open a fresh tab instead, same as no match at all.
      if (existingTab && !existingTab.pinned && !existingTab.group) {
        claimedTabs.add(existingTab);
        resolvedTabs.push(existingTab);
        mergedCount++;
        continue;
      }

      // One URL per call, not batched - openTabs()'s result doesn't
      // preserve positional correspondence to its input urls when some
      // fail, so batching here would make matching results back to
      // resolvedTabs ambiguous.
      const { openedTabs, failedUrls: openFailures } = await this.openTabs({
        urls: [url],
        window,
      });
      resolvedTabs.push(...openedTabs);
      failedUrls.push(...openFailures);
    }

    return { resolvedTabs, mergedCount, failedUrls };
  }

  /**
   * Ungroups tabs from a tab group, moving them back to regular tabs.
   *
   * @param {object} options
   * @param {string} options.groupId - ID of the tab group to ungroup
   * @param {Window} options.window - Browser window containing the tab group
   * @returns {Promise<{
   *   success: boolean,
   *   ungroupedTabs: Array<{
   *     linkedPanel: string,
   *     url: string,
   *     title: string
   *   }>,
   *   error?: string
   * }>} Result with ungrouped tabs and success status
   */
  async ungroupTabs({ groupId, window }) {
    if (!groupId || !window?.gBrowser) {
      return {
        success: false,
        ungroupedTabs: [],
        error: "Invalid parameters for ungrouping tabs",
      };
    }

    try {
      // Find the tab group by ID
      const group = window.gBrowser.tabGroups.find(g => g.id === groupId);

      if (!group) {
        return {
          success: false,
          ungroupedTabs: [],
          error: `Tab group with ID ${groupId} not found`,
        };
      }

      // Get all tabs in the group before ungrouping
      const tabsInGroup = [...group.tabs];
      const ungroupedTabs = tabsInGroup.map(tab => ({
        linkedPanel: tab.linkedPanel,
        url: tab.linkedBrowser?.currentURI?.spec || "",
        title: tab.label || "",
      }));

      // Ungroup each tab individually (removes them from the group but doesn't close them)
      for (const tab of tabsInGroup) {
        window.gBrowser.ungroupTab(tab);
      }

      return {
        success: true,
        ungroupedTabs,
      };
    } catch (error) {
      lazy.console.error("Failed to ungroup tabs:", error);
      return {
        success: false,
        ungroupedTabs: [],
        error: error.message || "Failed to ungroup tabs",
      };
    }
  }

  /**
   * Gets the next unused color for a new tab group.
   *
   * @param {Window} window - Browser window
   * @returns {string} Color code for the new group
   * @private
   */
  #getNextUnusedColor(window) {
    const usedColors = new Set(
      window.gBrowser.getAllTabGroups().map(group => group.color)
    );

    // Find the first unused color
    const color = TabManagementService.TAB_GROUP_COLORS.find(
      colorCode => !usedColors.has(colorCode)
    );

    if (color) {
      return color;
    }

    // If all colors are used, pick one randomly
    const randomIndex = Math.floor(
      Math.random() * TabManagementService.TAB_GROUP_COLORS.length
    );
    return TabManagementService.TAB_GROUP_COLORS[randomIndex] || "blue";
  }

  /**
   * Validates tabs for grouping and filters out invalid ones.
   *
   * @param {Array<Tab>} tabs - Tabs to validate
   * @param {Window} window - Browser window
   * @returns {{validTabs: Array<Tab>, failedTabs: Array}} Valid tabs and failed tabs with reasons
   * @private
   */
  #validateTabsForGrouping(tabs, window) {
    const validTabs = [];
    const failedTabs = [];

    tabs.forEach(tab => {
      // Check if tab belongs to the window
      const tabInWindow = tab?.linkedBrowser && tab.documentGlobal === window;

      if (!tabInWindow) {
        failedTabs.push({
          tab,
          reason: "invalid-tab",
        });
        return;
      }

      // Pinned tabs cannot be grouped
      if (tab.pinned) {
        failedTabs.push({
          tab,
          reason: "pinned-tab",
        });
        return;
      }

      // Tab already in a group
      if (tab.group) {
        failedTabs.push({
          tab,
          reason: "already-grouped",
        });
        return;
      }

      // Tab is closing
      if (tab.closing) {
        failedTabs.push({
          tab,
          reason: "tab-closing",
        });
        return;
      }

      validTabs.push(tab);
    });

    return { validTabs, failedTabs };
  }

  /**
   * Closes tabs based on provided tab data.
   *
   * @param {object} options
   * @param {Array<Tab>} options.tabs - Array of tab objects
   * @param {Window} options.window - Browser window containing the tabs
   * @returns {Promise<object>} Close summary
   */
  async closeTabs({ tabs, window }) {
    if (!tabs?.length) {
      lazy.console.warn("No tabs to close");

      return {
        requestedCount: 0,
        operationId: null,
        failedTabs: [],
      };
    }

    if (!window?.gBrowser) {
      throw new Error("Invalid browser window provided");
    }

    const failedTabs = [];
    const tabsToClose = this.#validateTabsForClosing(tabs, window, failedTabs);

    const { closedTabs, error } = await this.#performTabClosing(
      tabsToClose,
      window
    );

    let operationId = null;
    if (closedTabs.length) {
      operationId = this.storeClosedTabsForUndo({ closedTabs, window });
    }

    if (error) {
      lazy.console.error("Failed to close tabs:", error);
      failedTabs.push({
        reason: "exception",
        message: error.message,
      });
    }

    return {
      requestedCount: tabs.length,
      operationId,
      failedTabs,
    };
  }

  /**
   * Validates tabs and filters out invalid ones.
   *
   * @param {Array<Tab>} tabs - Tabs to validate
   * @param {Window} window - Browser window
   * @param {Array} failedTabs - Array to collect failed tabs
   * @returns {Array<Tab>} Valid tabs that can be closed
   * @private
   */
  #validateTabsForClosing(tabs, window, failedTabs) {
    return tabs.filter(tab => {
      const tabInWindow = tab?.linkedBrowser && tab.documentGlobal === window;

      if (!tabInWindow) {
        failedTabs.push({
          tab,
          reason: "invalid-tab",
        });
        return false;
      }

      if (tab.closing) {
        failedTabs.push({
          tab,
          reason: "already-closing",
        });
        return false;
      }

      return true;
    });
  }

  /**
   * Actually closes the validated tabs.
   *
   * @param {Array<Tab>} tabsToClose - Validated tabs to close
   * @param {Window} window - Browser window
   * @returns {Promise<object>} Object with closedTabs, and error (if any)
   * @private
   */
  async #performTabClosing(tabsToClose, window) {
    const closedTabs = [];
    let error = null;

    try {
      const operationTimestamp = Date.now();

      for (const browserTab of tabsToClose) {
        /**
         * Store lightweight metadata immediately before closing.
         *
         * SessionStore remains the source of truth for the actual restore data.
         */
        closedTabs.push({
          ...this.#getTabInfo(browserTab),
          operationTimestamp,
        });

        // Keep the window open when closing its last tab
        window.gBrowser.removeTab(browserTab, {
          closeWindowWithLastTab: false,
        });
      }
    } catch (err) {
      error = err;
    }

    return { closedTabs, error };
  }

  #compareClosedTabTimestamps(matches, operationTimestamp) {
    const targetTime = operationTimestamp || 0;
    let bestMatch = matches[0];
    let smallestDiff = Math.abs(bestMatch.closedAt - targetTime);

    for (const match of matches.slice(1)) {
      const diff = Math.abs(match.closedAt - targetTime);
      if (diff < smallestDiff) {
        smallestDiff = diff;
        bestMatch = match;
      }
    }

    return bestMatch.index;
  }

  /**
   * Finds the current SessionStore closed-tab index for a tab that belonged
   * to a specific close operation.
   *
   * @param {Window} window - Browser window
   * @param {object} operationTab - Tab metadata from the close operation
   * @returns {number|null} Index in SessionStore's closed-tab list, or null if not found
   * @private
   */
  #findClosedTabIndexForOperationTab(window, operationTab) {
    const closedTabData = this.#getClosedTabData(window);

    const matches = [];
    for (const [index, closedTab] of closedTabData.entries()) {
      if (this.#closedTabMatchesOperationTab(closedTab, operationTab)) {
        // SessionStore stores closedAt timestamp in milliseconds
        const closedAt = closedTab.closedAt || closedTab.state?.closedAt || 0;
        matches.push({ index, closedAt });
      }
    }

    if (!matches.length) {
      return null;
    }

    if (matches.length === 1) {
      return matches[0].index;
    }

    return this.#compareClosedTabTimestamps(
      matches,
      operationTab.operationTimestamp
    );
  }

  /**
   * Checks whether a SessionStore closed-tab entry matches a tab from this
   * close operation.
   *
   * @param {object} closedTab - SessionStore closed-tab entry
   * @param {object} operationTab - Tab metadata from the close operation
   * @returns {boolean} True if tabs match
   */
  #closedTabMatchesOperationTab(closedTab, operationTab) {
    const closedTabInfo = this.#normalizeClosedTab(closedTab);
    if (!closedTabInfo.url || !operationTab.url) {
      return false;
    }
    const urlsMatch = operationTab.url === closedTabInfo.url;
    const userContextIdsMatch =
      closedTabInfo.userContextId === operationTab.userContextId;

    return urlsMatch && userContextIdsMatch;
  }

  /**
   * Reads closed-tab data from SessionStore.
   *
   * @param {Window} window
   * @returns {Array<object>}
   * @private
   */
  #getClosedTabData(window) {
    const closedTabData = this.#sessionStore.getClosedTabDataForWindow(window);
    return Array.isArray(closedTabData) ? closedTabData : [];
  }

  /**
   * Normalizes a SessionStore closed-tab entry into the small amount of data
   * this service needs for matching.
   *
   * @param {object} closedTab - SessionStore closed-tab entry
   * @returns {{
   *   url: string | null,
   *   title: string | null,
   *   userContextId: number,
   *   pinned: boolean | null
   * }} Normalized tab data for matching
   * @private
   */
  #normalizeClosedTab(closedTab) {
    const state = closedTab?.state ?? closedTab ?? {};
    const entries = state.entries ?? [];

    // SessionStore uses 1-based indices.
    const activeIndex = Math.max((state.index ?? 1) - 1, 0);
    const activeEntry = entries[activeIndex] ?? entries.at?.(-1);
    const url = activeEntry?.url ?? state.url ?? null;
    const title = activeEntry?.title ?? state.title ?? closedTab?.title ?? null;

    const userContextId =
      state.userContextId ??
      state.originAttributes?.userContextId ??
      closedTab?.userContextId ??
      0;

    const pinned = typeof state.pinned === "boolean" ? state.pinned : null;

    return { url, title, userContextId, pinned };
  }

  /**
   * Creates lightweight tab info for UI/model disambiguation and later
   * SessionStore matching.
   *
   * @param {Tab} tab - Firefox tab object
   * @returns {{
   *   tabId: string | null,
   *   url: string | null,
   *   title: string,
   *   userContextId: number
   * }} Minimal tab metadata needed for matching
   * @private
   */
  #getTabInfo(tab) {
    const browser = tab.linkedBrowser;
    const principal = browser.contentPrincipal;
    const userContextId =
      principal?.originAttributes?.userContextId || tab.userContextId || 0;

    return {
      tabId: tab.getAttribute("data-tab-id") || null,
      url: browser.currentURI?.spec || null,
      title: tab.label,
      userContextId,
    };
  }
}

export const tabManagementService = new TabManagementService();
