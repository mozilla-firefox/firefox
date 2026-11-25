/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @import { TabSelectionData } from "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  tabManagementService:
    "moz-src:///browser/components/aiwindow/ui/modules/TabManagementService.sys.mjs",
  console() {
    return console.createInstance({
      logModule: "ToolUI",
      maxLogLevelPref: "browser.smartwindow.toolUI.logLevel",
    });
  },
});

/**
 * Resolves the tabs a user confirmed on a tool card and closes, groups or
 * opens them.
 */
export class ToolTabActions {
  /**
   * Resolve selected tabs to live tab objects across all active Smart Windows
   * by permanentKey
   *
   * @param {Array<TabSelectionData>} selectedTabs - Selected tabs
   * @param {Map<string, object>} tokenToKey - token -> permanentKey for this operation
   * @param {ChromeWindow} win - The browser window object
   * @returns {Map<ChromeWindow, Array<Tab>>|null} Verified tabs grouped by their
   *   owning window, or null if none valid
   * @private
   */
  static #verifyAndCollectTabs(selectedTabs = [], tokenToKey = null, win) {
    if (!win) {
      lazy.console.error("No browser window provided");
      return null;
    }
    if (!tokenToKey?.size) {
      lazy.console.warn("No tab-key map for this operation");
      return null;
    }

    const candidateWindows = lazy.BrowserWindowTracker.orderedWindows.filter(
      candidateWin => lazy.AIWindow.isAIWindowActive(candidateWin)
    );
    const claimedTabs = new Set();
    const tabsByWindow = new Map();
    let verifiedCount = 0;

    for (const selectedTab of selectedTabs) {
      const permanentKey =
        selectedTab.token && tokenToKey.get(selectedTab.token);

      let match = null;
      if (permanentKey) {
        for (const candidateWindow of candidateWindows) {
          const tab = candidateWindow.gBrowser.tabs.find(
            t => !claimedTabs.has(t) && t.permanentKey === permanentKey
          );
          if (tab) {
            match = { tab, window: candidateWindow };
            break;
          }
        }
      }

      if (!match) {
        lazy.console.warn(
          `No live tab for selection ${selectedTab.url} (token ${selectedTab.token})`
        );
        continue;
      }

      claimedTabs.add(match.tab);
      if (!tabsByWindow.has(match.window)) {
        tabsByWindow.set(match.window, []);
      }
      tabsByWindow.get(match.window).push(match.tab);
      verifiedCount++;
    }

    if (verifiedCount === 0) {
      lazy.console.warn("No valid tabs after verification");
      return null;
    }

    return tabsByWindow;
  }

  /**
   * Close the selected tabs after resolving them by permanentKey
   *
   * @param {Array<TabSelectionData>} selectedTabs - Selected tabs
   * @param {Map<string, object>} tokenToKey - token -> permanentKey for this operation
   * @param {ChromeWindow} win - The interacting browser window object
   * @returns {Promise<{operationIds: string[], requestedCount: number, failedTabs: Array}|null>}
   */
  static async closeSelectedTabs(selectedTabs = [], tokenToKey, win) {
    const tabsByWindow = this.#verifyAndCollectTabs(
      selectedTabs,
      tokenToKey,
      win
    );
    if (!tabsByWindow) {
      return null;
    }

    const operationIds = [];
    let requestedCount = 0;
    const failedTabs = [];

    for (const [ownerWindow, tabs] of tabsByWindow) {
      const activeTab = tabs.find(
        tab => tab === ownerWindow.gBrowser.selectedTab
      );
      if (activeTab) {
        activeTab.smartWindowActionSource = "close_current_tab";
      }

      const result = await lazy.tabManagementService.closeTabs({
        tabs,
        window: ownerWindow,
      });
      requestedCount += result.requestedCount;
      if (result.failedTabs.length) {
        failedTabs.push(...result.failedTabs);
      }
      if (result.operationId) {
        operationIds.push(result.operationId);
      }
    }

    return { operationIds, requestedCount, failedTabs };
  }

  /**
   * Whether the chat tab can go into the group we are about to create.
   *
   * A tab group can only hold tabs from one window, so we skip the chat tab
   * if the group is being made in a different window. We also skip it if it
   * is in a split view, because adding it would move the whole split view and
   * pull in the other tab too. A pinned or grouped chat tab also stays where it
   * is. Closing tabs are checked by TabManagementService instead.
   *
   * @param {MozTabbrowserTab} [originTab] - The chat tab
   * @param {Array<MozTabbrowserTab>} groupTabs - Tabs already going into the group
   * @param {ChromeWindow} groupWindow - Window the group is created in
   * @returns {boolean} True if we should add originTab to the group
   * @private
   */
  static #canOriginTabJoinGroup(originTab, groupTabs, groupWindow) {
    return !!(
      originTab &&
      originTab.documentGlobal === groupWindow &&
      !originTab.splitview &&
      !originTab.pinned &&
      !originTab.group &&
      !groupTabs.includes(originTab)
    );
  }

  /**
   * The tab a conversation is open in, within the given window.
   *
   * getChatTabConversationId only answers for tabs that are the chat page
   * itself, so a sidebar conversation does not match here even though its id
   * is recorded against the web page tab beside the sidebar.
   *
   * Every Smart Window is searched rather than one in particular, because a
   * conversation id is unique and the chat may not be in the window the group
   * ends up in. createTabGroup drops it again if the windows differ.
   *
   * @param {string} conversationId - Id of the conversation to find
   * @returns {MozTabbrowserTab|null} The chat tab, or null if it is not in a tab
   */
  static findChatTab(conversationId) {
    if (!conversationId) {
      return null;
    }
    for (const win of lazy.BrowserWindowTracker.orderedWindows) {
      if (win.closed || !win.gBrowser || !lazy.AIWindow.isAIWindowActive(win)) {
        continue;
      }
      const tab = [...win.gBrowser.tabs].find(
        candidate =>
          lazy.AIWindow.getChatTabConversationId(candidate) === conversationId
      );
      if (tab) {
        return tab;
      }
    }
    return null;
  }

  /**
   * Drops the chat tab when it is the only tab left that could be grouped.
   *
   * The user asked for their own tabs to be grouped, so if all of those turn
   * out to be closing, a group holding nothing but the chat tab is not what
   * they wanted. Leaving it out means createTabGroup finds nothing to group and
   * reports the failure itself.
   *
   * A selection of just the chat tab is left alone, since that is a real
   * choice rather than everything else falling away.
   *
   * @param {Array<MozTabbrowserTab>} windowTabs - Resolved tabs in the group's window
   * @param {ChromeWindow} groupWindow - Window the group is created in
   * @param {Array<TabSelectionData>} selections - What the user confirmed
   * @param {Map<string, object>} tokenToKey - token -> permanentKey
   * @returns {Array<MozTabbrowserTab>} The tabs to hand to the service
   * @private
   */
  static #dropLoneChatTab(windowTabs, groupWindow, selections, tokenToKey) {
    const chatSelection = selections.find(selection => selection.isChatTab);
    if (!chatSelection || selections.length === 1) {
      return windowTabs;
    }

    const chatKey = tokenToKey?.get(chatSelection.token);
    const groupable = windowTabs.filter(
      tab => !lazy.tabManagementService.getGroupingRejection(tab, groupWindow)
    );
    if (groupable.length !== 1 || groupable[0].permanentKey !== chatKey) {
      return windowTabs;
    }

    return windowTabs.filter(tab => tab.permanentKey !== chatKey);
  }

  /**
   * Creates a tab group from selected tabs after verification.
   *
   * @param {object} options - Options for creating the tab group
   * @param {Array<TabSelectionData>} options.tabs - Selected tabs (each carrying a `token`)
   * @param {Map<string, object>} options.tokenToKey - token -> permanentKey for this operation
   * @param {ChromeWindow} options.window - The browser window object
   * @param {string} [options.label="Tab Group"] - Label for the tab group
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
   * } | null>} Creation result with group details and success status, or null if no valid tabs
   */
  static async createTabGroup({
    tabs = [],
    tokenToKey = null,
    window: win,
    label,
  }) {
    const tabsByWindow = this.#verifyAndCollectTabs(tabs, tokenToKey, win);
    if (!tabsByWindow) {
      return null;
    }

    // Tabs in tab groups only span a single window
    const chatKey = tokenToKey.get(tabs.find(tab => tab.isChatTab)?.token);
    const countRequested = (ownerTabs = []) =>
      ownerTabs.filter(tab => tab.permanentKey !== chatKey).length;
    let groupWindow = countRequested(tabsByWindow.get(win)) ? win : null;
    if (!groupWindow) {
      for (const [ownerWindow, ownerTabs] of tabsByWindow) {
        if (
          !groupWindow ||
          countRequested(ownerTabs) >
            countRequested(tabsByWindow.get(groupWindow))
        ) {
          groupWindow = ownerWindow;
        }
      }
    }

    const result = await lazy.tabManagementService.createTabGroup({
      tabs: this.#dropLoneChatTab(
        tabsByWindow.get(groupWindow),
        groupWindow,
        tabs,
        tokenToKey
      ),
      window: groupWindow,
      label,
    });

    // Report tabs from other windows that were not included
    for (const [ownerWindow, ownerTabs] of tabsByWindow) {
      if (ownerWindow === groupWindow) {
        continue;
      }
      for (const tab of ownerTabs) {
        result.failedTabs.push({ tab, reason: "other-window" });
      }
    }

    return result;
  }

  /**
   * Opens the given tabs and groups the result. Unlike createTabGroup, does
   * not require the tabs to already be open - any selection that matches a
   * tab already open in the window is reused instead of duplicated (see
   * TabManagementService.resolveOrOpenTabs).
   *
   * @param {object} options
   * @param {Array<TabSelectionData>} options.tabs - Tabs to open and group
   * @param {ChromeWindow} options.window - The browser window
   * @param {string} options.label - Tab group label
   * @param {MozTabbrowserTab} [options.originTab] - The tab the chat is in,
   *   so we can put it in the group as well
   * @returns {Promise<object|null>} Result of createTabGroup (plus
   *   mergedCount - how many selected tabs were already open rather than
   *   newly opened), or null if there were no tabs to open or none resolved
   *   successfully
   */
  static async openAndGroupTabs({ tabs = [], window: win, label, originTab }) {
    if (!tabs.length) {
      lazy.console.warn("No tabs to open");
      return null;
    }

    const { resolvedTabs, mergedCount } =
      await lazy.tabManagementService.resolveOrOpenTabs({
        tabs,
        window: win,
      });

    if (!resolvedTabs.length) {
      return null;
    }

    // Put the chat tab in the group too. It is never one of the resolved tabs,
    // because its chrome: URL gets filtered out before the model sees it, so
    // we have to add it ourselves.
    //
    // Only do that if one of the requested tabs can actually be grouped. If
    // they are all closing, createTabGroup would drop them all and we would
    // be left with a group holding just the chat tab, which is not what was
    // asked for.
    const canGroupARequestedTab = resolvedTabs.some(
      tab => !lazy.tabManagementService.getGroupingRejection(tab, win)
    );
    const groupTabs =
      canGroupARequestedTab &&
      this.#canOriginTabJoinGroup(originTab, resolvedTabs, win)
        ? [originTab, ...resolvedTabs]
        : resolvedTabs;

    const result = await lazy.tabManagementService.createTabGroup({
      tabs: groupTabs,
      window: win,
      label,
    });

    return { ...result, mergedCount };
  }

  /**
   * Resolves a single selected tab: switches to it if it's already open,
   * otherwise opens it as a new background tab and switches to that -
   * never navigates the current tab, since that may be the tab hosting
   * the fullpage conversation itself.
   *
   * @param {object} options
   * @param {TabSelectionData} options.tab - The single selected tab
   * @param {ChromeWindow} options.window - The browser window
   * @returns {Promise<{success: boolean, switched: boolean}>}
   */
  static async openOrSwitchToTab({ tab, window: win }) {
    const existingTab = lazy.tabManagementService.findOpenTab({
      url: tab.url,
      window: win,
    });

    if (existingTab) {
      lazy.tabManagementService.switchToTab({ tab: existingTab, window: win });
      return { success: true, switched: true };
    }

    const { openedTabs } = lazy.tabManagementService.openTabs({
      urls: [tab.url],
      window: win,
    });

    if (!openedTabs.length) {
      return { success: false, switched: false };
    }

    lazy.tabManagementService.switchToTab({ tab: openedTabs[0], window: win });
    return { success: true, switched: false };
  }

  /**
   * Opens or switches to a single tab, or opens and groups multiple tabs.
   *
   * @param {object} options
   * @param {Array<TabSelectionData>} options.tabs - Tabs to open
   * @param {ChromeWindow} options.window - The browser window
   * @param {string} options.label - Tab group label, used only when grouping
   * @param {MozTabbrowserTab} [options.originTab] - See openAndGroupTabs
   * @returns {Promise<object|null>} Result of openOrSwitchToTab for a
   *   single tab, or of openAndGroupTabs otherwise
   */
  static async openOrGroupTabs({ tabs = [], window: win, label, originTab }) {
    if (tabs.length === 1) {
      return this.openOrSwitchToTab({ tab: tabs[0], window: win });
    }
    return this.openAndGroupTabs({ tabs, window: win, label, originTab });
  }
}
