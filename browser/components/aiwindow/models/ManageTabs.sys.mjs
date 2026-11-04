/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @import { ChatConversation } from "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
 */

import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ToolUI: "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs",
  ToolUITelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolUITelemetry.sys.mjs",
  SmartTabGroupingManager:
    "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs",
});

export const CLOSE_TABS = "close_tabs";
export const GROUP_TABS = "group_tabs";
export const TAB_ACTIONS = [CLOSE_TABS, GROUP_TABS];
const UI_TYPE_BY_ACTION = {
  [CLOSE_TABS]: "website-confirmation",
  [GROUP_TABS]: "tab-group-confirmation",
};

/**
 * Finds tabs in active AI windows whose URLs are in validUrls.
 *
 * @param {Set<string>} validUrls
 * @returns {{ matchedTabs: Array<object>, topAIWin: object|null }}
 */
function findMatchingAIWindowTabs(validUrls) {
  const matchedTabs = [];
  let topAIWin = null;

  for (const win of lazy.BrowserWindowTracker.orderedWindows) {
    if (!lazy.AIWindow.isAIWindowActive(win) || win.closed || !win.gBrowser) {
      continue;
    }
    if (!topAIWin) {
      topAIWin = win;
    }
    for (const tab of win.gBrowser.tabs) {
      const url = tab.linkedBrowser?.currentURI?.spec;
      if (validUrls.has(url)) {
        matchedTabs.push({ tab, win, url, linkedPanel: tab.linkedPanel });
      }
    }
  }

  return { matchedTabs, topAIWin };
}

/**
 * Returns true if the matched tabs require user confirmation before acting
 * (pinned/selected tabs, grouped tabs when grouping, current tab, all tabs of
 * the top AI window, or untrusted input).
 *
 * @param {Array<object>} tabs - list of tabs to check
 * @param {object} topAIWin - the top active AI window
 * @param {object} securityProperties - The security properties of the conversation.
 * @param {string} action - The tab action to take
 * @returns {boolean}
 */
function shouldRequireUserConfirmation(
  tabs,
  topAIWin,
  securityProperties,
  action
) {
  if (securityProperties?.untrustedInput) {
    return true;
  }
  if (
    tabs.some(
      ({ tab }) => tab.pinned === true || (action === GROUP_TABS && tab.group)
    )
  ) {
    return true;
  }
  if (tabs.some(({ tab, win }) => win.gBrowser.selectedTab === tab)) {
    return true;
  }
  if (topAIWin) {
    const topWinTabs = new Set(
      tabs.filter(({ win }) => win === topAIWin).map(({ tab }) => tab)
    );
    if (
      topWinTabs.size &&
      topAIWin.gBrowser.tabs.every(tab => topWinTabs.has(tab))
    ) {
      return true;
    }
  }
  return false;
}

/**
 * @param {{ validUrls: Set<string>, askConfirmation: boolean, includeChatTab: boolean, baseTelemetryInfo: object, conversation: ChatConversation, toolCallId?: string }} state
 * @param {{ action: string, verb: string, failureError: string, failureMessage: string,
 *           takeUIAction: (gathered: object) => Promise<object>,
 *           getToolResults: (result: object, gathered: object) => object }} toolHandler
 */
async function runManageTabsFlow(state, toolHandler) {
  const gatheredResult = await gatherTabs(
    state.validUrls,
    state.baseTelemetryInfo
  );

  if (gatheredResult.earlyResult) {
    return gatheredResult.earlyResult;
  }

  let needsConfirmation =
    state.askConfirmation ||
    shouldRequireUserConfirmation(
      gatheredResult.matchedTabs,
      gatheredResult.topAIWin,
      state.conversation.securityProperties,
      toolHandler.action
    );

  if (
    toolHandler.action === GROUP_TABS &&
    (needsConfirmation || state.includeChatTab)
  ) {
    offerChatTab(gatheredResult, state.conversation, state.includeChatTab);
    // Moving a pinned or grouped chat tab needs confirmation
    const chatTab = gatheredResult.tabs.find(tab => tab.isChatTab);
    if (chatTab?.pinned || chatTab?.inGroup) {
      needsConfirmation = true;
    }
  }

  if (needsConfirmation) {
    const uiType = UI_TYPE_BY_ACTION[toolHandler.action];
    if (!uiType) {
      throw new Error(`No UI mapping for action: ${toolHandler.action}`);
    }
    // Keep token -> permanentKey on the chrome side until the user confirms;
    // the map can't ride through the confirmation actor round-trip.
    lazy.ToolUI.registerTabKeys(state.toolCallId, gatheredResult.tabKeyByToken);
    // ToolUI records the matching `browser_action_complete` after user confirmation.
    state.conversation.stashPendingBrowserActionTelemetry(
      state.toolCallId,
      state.baseTelemetryInfo
    );
    return {
      toolResult: {
        description: `The following tabs were found. User confirmation is required to ${toolHandler.verb} them.`,
        pending: true,
        action: toolHandler.action,
        selectedTabs: gatheredResult.summarizedTabInfo,
      },
      uiData: {
        uiType,
        properties: await toolHandler.getConfirmationProperties(gatheredResult),
      },
    };
  }

  const result = await toolHandler.takeUIAction(gatheredResult);

  if (!result || !result.operationIds?.length) {
    lazy.ToolUITelemetry.recordBrowserActionComplete({
      ...state.baseTelemetryInfo,
      result: "error",
      tabs_affected: 0,
      undo_available: false,
      error: toolHandler.failureError,
    });
    return {
      toolResult: `Error: ${toolHandler.action} action failed`,
      uiData: null,
    };
  }

  const { description, selectedTabs, telemetryValues } =
    toolHandler.getToolResults(result, gatheredResult);

  lazy.ToolUITelemetry.recordBrowserActionComplete({
    ...state.baseTelemetryInfo,
    result: telemetryValues.telemetryResult,
    tabs_affected: telemetryValues.affectedCount,
    undo_available: telemetryValues.affectedCount > 0,
    error: telemetryValues.failedCount ? toolHandler.failureMessage : "",
  });

  return {
    toolResult: {
      description,
      selectedTabs,
    },
    uiData: {
      uiType: "ai-action-result",
      properties: {
        confirmedData: {
          selectedTabs: gatheredResult.tabs,
          operationIds: result.operationIds,
          actionTimestamp: Date.now(),
          actionType: toolHandler.action,
        },
      },
    },
  };
}

/**
 * Adds the chat tab to the tabs the confirmation card offers, so the user can
 * see whether it is going into the group and change the selection.
 *
 * It is deliberately kept out of matchedTabs, which decides whether a card is
 * shown at all, and out of the summary sent to the model, which is never told
 * about chrome: URLs.
 *
 * @param {object} gathered - Result of gatherTabs, updated in place
 * @param {ChatConversation} conversation - Conversation the request came from
 * @param {boolean} checked - Whether the chat tab starts ticked
 */
function offerChatTab(gathered, conversation, checked) {
  const chatTab = lazy.ToolUI.findChatTab(conversation?.id);
  if (
    !chatTab ||
    !gathered.matchedTabs.some(({ win }) => win === chatTab.documentGlobal)
  ) {
    return;
  }

  const token = Services.uuid.generateUUID().toString();
  gathered.tabKeyByToken.set(token, chatTab.permanentKey);
  gathered.tabs.push({
    token,
    url: chatTab.linkedBrowser?.currentURI?.spec ?? "",
    title: sanitizeUntrustedContent(chatTab.label),
    userContextId: chatTab.userContextId,
    pinned: chatTab.pinned,
    inGroup: !!chatTab.group,
    selected: chatTab.documentGlobal?.gBrowser?.selectedTab === chatTab,
    iconSrc: "",
    checked,
    // Lets createTabGroup tell this row apart from the tabs the user asked
    // for, so it can avoid making a group out of the chat tab alone and
    // keeps it out of the result sent to the model.
    isChatTab: true,
  });
}

async function gatherTabs(validUrls, baseTelemetryInfo) {
  const { matchedTabs, topAIWin } = findMatchingAIWindowTabs(validUrls);

  if (!matchedTabs.length) {
    lazy.ToolUITelemetry.recordBrowserActionComplete({
      ...baseTelemetryInfo,
      result: "no_match",
      tabs_affected: 0,
      undo_available: false,
      error: "no_open_tab_match",
    });
    return {
      earlyResult: {
        toolResult: "Error: None of the provided URL tokens match an open tab.",
        uiData: null,
      },
    };
  }

  // Identify each tab by its browser's permanentKey
  const tabKeyByToken = new Map();
  const tabs = matchedTabs.map(({ tab, win, url }) => {
    const token = Services.uuid.generateUUID().toString();
    tabKeyByToken.set(token, tab.permanentKey);
    return {
      token,
      url,
      title: sanitizeUntrustedContent(tab.label),
      userContextId: tab.userContextId,
      pinned: tab.pinned,
      inGroup: !!tab.group,
      selected: win.gBrowser.selectedTab === tab,
      iconSrc: url ? `page-icon:${url}` : "",
      checked: true,
    };
  });

  const summarizedTabInfo = tabs.map(({ url, title, checked }) => ({
    url,
    title,
    checked,
  }));

  return {
    earlyResult: null,
    summarizedTabInfo,
    tabs,
    matchedTabs,
    topAIWin,
    tabKeyByToken,
  };
}

/**
 * Closes the tabs identified by `validUrls` in the active AI windows.
 */
const closeTabsToolHandler = {
  action: CLOSE_TABS,
  verb: "close",
  failureError: "close_failed",
  failureMessage: "some_tabs_failed_to_close",

  async takeUIAction(gatheredResult) {
    return await lazy.ToolUI.closeSelectedTabs(
      gatheredResult.tabs,
      gatheredResult.tabKeyByToken,
      gatheredResult.topAIWin
    );
  },

  getConfirmationProperties(gatheredResult) {
    return { tabs: gatheredResult.tabs };
  },

  getToolResults(result, gatheredResult) {
    const failedKeys = new Set(
      (result.failedTabs ?? [])
        .map(failedTab => failedTab.tab?.permanentKey)
        .filter(Boolean)
    );
    const closedTabs = gatheredResult.tabs.map(({ url, title, token }) => ({
      url,
      title,
      closed: !failedKeys.has(gatheredResult.tabKeyByToken.get(token)),
    }));

    const closedCount = closedTabs.filter(tab => tab.closed).length;
    const failedCount = closedTabs.length - closedCount;
    const telemetryResult = lazy.ToolUITelemetry.browserActionResult(
      closedCount,
      closedTabs.length
    );

    return {
      description: failedCount
        ? `Some tabs failed to close (${failedCount} of ${closedTabs.length}).`
        : "Tabs were successfully closed.",
      selectedTabs: closedTabs,
      telemetryValues: {
        telemetryResult,
        affectedCount: closedCount,
        failedCount,
      },
    };
  },
};

/**
 * Builds a tool handler that groups the tabs identified by `validUrls` into a new
 * tab group in the top active AI window. Uses the optional caller-supplied
 * `rawLabel` when present (sanitized, capped at 40 chars); otherwise predicts
 * one via `SmartTabGroupingManager`, defaulting to "Tabs" when prediction fails.
 *
 * @param {string} rawLabel
 */
function makeGroupTabsToolHandler(rawLabel) {
  const getLabel = async gatheredResult => {
    if (rawLabel) {
      return sanitizeUntrustedContent(rawLabel, true).slice(0, 40).trim();
    }
    const rawTabs = gatheredResult.matchedTabs
      .filter(m => m.win === gatheredResult.topAIWin)
      .map(m => m.tab);

    const allVisible = gatheredResult.topAIWin.gBrowser.visibleTabs;
    const otherTabs = allVisible.filter(t => !rawTabs.includes(t) && !t.pinned);

    const manager = new lazy.SmartTabGroupingManager();
    return (
      (await manager.getPredictedLabelForGroup(rawTabs, otherTabs)) || "Tabs"
    );
  };

  return {
    action: GROUP_TABS,
    verb: "group",
    failureError: "group_failed",
    failureMessage: "some_tabs_could_not_be_grouped",

    async getConfirmationProperties(gatheredResult) {
      return {
        tabs: gatheredResult.tabs,
        tabGroupLabel: await getLabel(gatheredResult),
      };
    },

    async takeUIAction(gathered) {
      const label = await getLabel(gathered);
      const result = await lazy.ToolUI.createTabGroup({
        tabs: gathered.tabs,
        tokenToKey: gathered.tabKeyByToken,
        window: gathered.topAIWin,
        label,
      });
      // Tab groups span a single window so `createTabGroup` returns at most one
      // `group.id`.
      return {
        ...result,
        operationIds: result?.group?.id ? [result.group.id] : [],
        label,
      };
    },

    getToolResults(result, gatheredResult) {
      const failedPanels = new Set(
        (result.failedTabs ?? [])
          .map(failedTab => failedTab.tab?.linkedPanel)
          .filter(Boolean)
      );
      const groupedTabs = gatheredResult.tabs
        .filter(tab => !tab.isChatTab)
        .map(({ url, title, linkedPanel }) => ({
          url,
          title,
          grouped: !failedPanels.has(linkedPanel),
        }));

      const groupedCount = groupedTabs.filter(tab => tab.grouped).length;
      const failedCount = groupedTabs.length - groupedCount;
      const telemetryResult = lazy.ToolUITelemetry.browserActionResult(
        groupedCount,
        groupedTabs.length
      );

      return {
        description: failedCount
          ? `Some tabs could not be grouped (${failedCount} of ${groupedTabs.length}).`
          : `Successfully grouped ${groupedTabs.length} tabs in group ${result.label}.`,
        selectedTabs: groupedTabs,
        telemetryValues: {
          telemetryResult,
          affectedCount: groupedCount,
          failedCount,
        },
      };
    },
  };
}

/**
 * Tab management orchestrator to handle all tab-related actions.
 *
 * @param {{ action: string, validUrls: Set<string>, ask_confirmation: boolean, include_chat_tab?: boolean, label: string, baseTelemetryInfo: object, toolCallId?: string }} params
 * @param {ChatConversation} conversation
 * @returns {Promise<object>}
 */
export async function manageTabsAction(
  {
    action,
    validUrls,
    ask_confirmation,
    include_chat_tab = false,
    label = "",
    baseTelemetryInfo,
    toolCallId,
  },
  conversation
) {
  const HANDLERS = {
    [CLOSE_TABS]: () => closeTabsToolHandler,
    [GROUP_TABS]: () => makeGroupTabsToolHandler(label),
  };

  const makeHandler = HANDLERS[action];
  if (!makeHandler) {
    throw new Error(`Invalid action: ${action}`);
  }
  const toolHandler = makeHandler();

  return runManageTabsFlow(
    {
      validUrls,
      askConfirmation: ask_confirmation,
      includeChatTab: include_chat_tab,
      baseTelemetryInfo,
      conversation,
      toolCallId,
    },
    toolHandler
  );
}
