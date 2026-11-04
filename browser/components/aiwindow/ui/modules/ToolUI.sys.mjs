/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { isSmartPageURL } from "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs";

/**
 * @import { ChatConversation } from "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
 * @import { ChatMessage } from "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
 */

/**
 * Tab action result status.
 *
 * @typedef {"success" | "partial_success" | "error"} TabActionCompletion
 */

/**
 * @typedef {object} TabSelectionData
 * @property {string} linkedPanel - ID of the linked panel (e.g., "panel-3-1")
 * @property {string} url - URL of the tab
 * @property {string} title - Display title of the tab
 * @property {string} [iconSrc] - URL for the tab's favicon (optional)
 * @property {boolean} [checked] - Whether the tab is selected in UI (optional)
 */

/**
 * @typedef {object} ToolUpdateData
 * @property {"current" | "new"} [openTarget] - Where to open an AITab
 * @property {Array<TabSelectionData>} [selectedTabs] - Array of selected tabs
 * @property {Array<string>} [operationIds] - Undo handles for the action
 * @property {boolean} [wasRestored] - Flag indicating tabs were restored
 * @property {number} [restoredCount] - Number of tabs restored
 * @property {Array<TabSelectionData>} [originalClosedTabs] - Original tabs that were closed
 */

/**
 * @typedef {object} HandlerContext
 * @property {object} message - Message containing the tool UI
 * @property {string} toolCallId - ID of the tool call
 * @property {ToolUpdateData} updateData - Update data for the handler
 * @property {object} conversation - Conversation object
 * @property {ChromeWindow} window - Browser window object
 * @property {object} originalData - Original update data passed to handleUpdate
 * @property {string} [mode] - Smart Window mode for telemetry
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  tabManagementService:
    "moz-src:///browser/components/aiwindow/ui/modules/TabManagementService.sys.mjs",
  ToolUITelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolUITelemetry.sys.mjs",
  MESSAGE_ROLE:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs",
  SmartWindowTelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({
    prefix: "ToolUI",
  });
});

/**
 * UI labels for tool results and follow-ups.
 */
export const UI_TYPES = {
  AITAB: "aitab",
  WEBSITE_CONFIRMATION: "website-confirmation",
  TAB_GROUP_CONFIRMATION: "tab-group-confirmation",
  AI_ACTION_RESULT: "ai-action-result",
  CANCELLED_COMPONENT: "cancelled-component",
  RETRY_COMPONENT: "retry-component",
};

/**
 * UI update types for communicating user interactions with tool UIs back to the actor.
 */
export const UI_UPDATE_TYPES = {
  OPEN_AITAB: "open-aitab",
  CONFIRMATION_TAB_SELECTION: "confirmation-tab-selection",
  CANCEL_TAB_SELECTION: "cancel-tab-selection",
  CONFIRM_TAB_GROUP_SELECTION: "confirm-tab-group-selection",
  CONFIRM_OPEN_AND_GROUP_TABS_SELECTION:
    "confirm-open-and-group-tabs-selection",
  TOGGLE_ALL_TAB_SELECTION: "toggle-all-tab-selection",
  UNDO_TAB_CLOSE: "undo-tab-close",
  UNDO_TAB_GROUP: "undo-tab-group",
  RETRY_PROMPT: "retry-prompt",
};

/**
 * UI types that represent confirmation dialogs with stateful behavior
 * These UIs can be cancelled and need special handling when restored from DB
 */
export const CONFIRMATION_UI_TYPES = [
  UI_TYPES.WEBSITE_CONFIRMATION,
  UI_TYPES.TAB_GROUP_CONFIRMATION,
];

/**
 * Confirmation UI types that record a `browser_action_prompt`.
 */
const PROMPT_ACTION_BY_UI_TYPE = {
  [UI_TYPES.WEBSITE_CONFIRMATION]: "close_tabs",
  [UI_TYPES.TAB_GROUP_CONFIRMATION]: "group_tabs",
};

/**
 * `prompt_type` values recorded on the tab confirmation telemetry events.
 */
const PROMPT_TYPES = {
  SAFETY_CONFIRMATION: "safety_confirmation",
};

/**
 * `toggle` values recorded when "Select all" or "Deselect all" is clicked.
 */
const TOGGLE_ALL_TYPES = {
  SELECT_ALL: "select_all",
  DESELECT_ALL: "deselect_all",
};

/**
 * Description for the message sent back to the model after a tab-selection
 * confirmation resolves, shared by every action type's confirm handler.
 */
const SELECTED_TABS_CONFIRMATION_DESCRIPTION =
  "User confirmed the requested action. selectedTabs contains the tabs that were acted upon.";

/**
 * Manages the Tool UI updates and orchestrates state changes for tool UI components
 */
export class ToolUI {
  /**
   * Per-confirmation map of selection token to browser permanentKey, keyed by
   * toolCallId. Populated when a tab confirmation is shown and read back when
   * the user confirms, so each selected tab can be resolved back to a stable
   * identity
   *
   * @type {Map<string, Map<string, object>>} toolCallId -> (token -> permanentKey)
   */
  static #tabKeysByToolCall = new Map();

  /**
   * Register the token to permanentKey map for a close tabs confirmation.
   *
   * @param {string} toolCallId
   * @param {Map<string, object>} tokenToKey
   */
  static registerTabKeys(toolCallId, tokenToKey) {
    if (toolCallId && tokenToKey?.size) {
      this.#tabKeysByToolCall.set(toolCallId, tokenToKey);
    }
  }

  /**
   * Drop a confirmation's token map once it resolves or is cancelled
   *
   * @param {string} toolCallId
   */
  static clearTabKeys(toolCallId) {
    this.#tabKeysByToolCall.delete(toolCallId);
  }

  static #getConfirmationReason(tabs, actionType) {
    if (tabs.some(t => t.pinned)) {
      return "pinned_tab";
    }
    if (actionType === "group_tabs" && tabs.some(t => t.inGroup)) {
      return "grouped_tab";
    }
    if (tabs.some(t => t.selected)) {
      return "active_tab";
    }
    if (tabs.length === 1) {
      return "last_tab";
    }
    return "user_action";
  }

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

  /* ========================================================================
   * Tool UI Update Handlers
   * ======================================================================== */

  /**
   * Records the user response to a tab confirmation.
   *
   * @param {object} options
   * @param {HandlerContext} options.context - Handler context
   * @param {string} options.actionType - Action the confirmation was for
   * @param {"confirm" | "cancel"} options.response - The user's response
   * @param {number} options.selected - Number of tabs the user acted on
   * @param {string} [options.reason] - Why the prompt was shown
   */
  static #recordTabConfirmationResponse({
    context,
    actionType,
    response,
    selected,
    reason = "user_action",
  }) {
    const { conversation, mode } = context;

    lazy.ToolUITelemetry.recordBrowserActionPromptResponse({
      location: mode,
      chat_id: conversation.id,
      message_seq: conversation.messageCount,
      action: actionType,
      prompt_type: PROMPT_TYPES.SAFETY_CONFIRMATION,
      response,
      selected,
      reason,
    });
  }

  /**
   * Records browser_action_complete using the context stashed at submit time.
   *
   * @param {object} options
   * @param {HandlerContext} options.context - Handler context
   * @param {TabActionCompletion | "cancelled"} options.result - Action outcome
   * @param {number} [options.tabsAffected] - Tabs the action affected
   * @param {boolean} [options.undoAvailable] - Whether undo was offered
   * @param {string} [options.error] - Error code when unsuccessful
   */
  static #recordConfirmedBrowserActionComplete({
    context,
    result,
    tabsAffected = 0,
    undoAvailable = false,
    error = "",
  }) {
    const { conversation, toolCallId } = context;
    const baseTelemetryInfo =
      conversation.takePendingBrowserActionTelemetry(toolCallId);
    if (!baseTelemetryInfo) {
      return;
    }

    lazy.ToolUITelemetry.recordBrowserActionComplete({
      ...baseTelemetryInfo,
      result,
      tabs_affected: tabsAffected,
      undo_available: undoAvailable,
      error,
    });
  }

  /**
   * Returns the shared `browserActionResult` outcome.
   *
   * @param {number} affected - Tabs successfully acted on
   * @param {number} requested - Tabs the user selected
   * @param {string} failureMessage - Error code to use when any tab failed
   * @returns {{result: TabActionCompletion, tabsAffected: number,
   *   error: string}}
   */
  static #summarizeTabActionOutcome(affected, requested, failureMessage) {
    return {
      result: lazy.ToolUITelemetry.browserActionResult(affected, requested),
      tabsAffected: affected,
      error: affected < requested ? failureMessage : "",
    };
  }

  /**
   * Records accepted confirmations for tabs that are not available anymore.
   *
   * @param {object} options
   * @param {HandlerContext} options.context - Handler context
   * @param {string} options.actionType - Action the confirmation was for
   * @param {Array<TabSelectionData>} options.selectedTabs - Tabs the user selected
   * @param {string} options.error - Error code explaining the failure
   */
  static #recordAbandonedTabConfirmation({
    context,
    actionType,
    selectedTabs,
    error,
  }) {
    this.#recordTabConfirmationResponse({
      context,
      actionType,
      response: "confirm",
      selected: selectedTabs.length,
    });
    this.#recordConfirmedBrowserActionComplete({
      context,
      result: "error",
      error,
    });
  }

  /**
   * Finalizes a tab-selection confirmation, shared by close_tabs,
   * group_tabs, and open_tabs: records prompt-response telemetry, updates
   * the tool UI with the action result, and resolves the pending tool
   * confirmation so the conversation can continue.
   *
   * @param {object} options
   * @param {HandlerContext} options.context - Handler context
   * @param {string} options.actionType - Action type recorded in
   *   telemetry and updateData (e.g. "close_tabs", "group_tabs")
   * @param {Array<TabSelectionData>} options.selectedTabs - Tabs the user
   *   selected/acted on
   * @param {object} [options.extraUpdateData] - Action-specific fields to
   *   merge into updateData (e.g. operationId, group, mergedCount)
   * @param {object} [options.resultInfo] - Result info for browser_action_complete
   */
  static #finalizeTabActionConfirmation({
    context,
    actionType,
    selectedTabs,
    extraUpdateData = {},
    resultInfo = null,
  }) {
    const { updateData, message, conversation, originalData, toolCallId } =
      context;

    this.#recordTabConfirmationResponse({
      context,
      actionType,
      response: "confirm",
      selected: selectedTabs.length,
    });

    if (resultInfo) {
      this.#recordConfirmedBrowserActionComplete({ context, ...resultInfo });
    }

    const enhancedData = {
      ...originalData,
      updateData: {
        ...updateData,
        actionTimestamp: Date.now(),
        actionType,
        ...extraUpdateData,
      },
    };

    conversation.updateToolUI(message, enhancedData, UI_TYPES.AI_ACTION_RESULT);

    const confirmationMessage = {
      description: SELECTED_TABS_CONFIRMATION_DESCRIPTION,
      selectedTabs: selectedTabs
        .filter(tab => !tab.isChatTab)
        .map(({ url, title }) => ({ url, title })),
    };

    const pendingAction =
      conversation.getPendingToolConfirmation(toolCallId)?.content.body.action;
    if (pendingAction) {
      confirmationMessage.action = pendingAction;
    }
    conversation.resolvePendingToolConfirmation(
      confirmationMessage,
      toolCallId
    );
  }

  /**
   * Handler for tab selection confirmation
   *
   * @param {HandlerContext} context - Handler context
   * @returns {Promise<boolean>} True if successful
   * @private
   */
  static async #handleConfirmationTabSelection(context) {
    const { updateData, window, toolCallId } = context;
    const { selectedTabs = [] } = updateData ?? {};

    const tokenToKey = this.#tabKeysByToolCall.get(toolCallId);
    const result = await this.closeSelectedTabs(
      selectedTabs,
      tokenToKey,
      window
    );
    this.clearTabKeys(toolCallId);
    if (!result) {
      this.#recordAbandonedTabConfirmation({
        context,
        actionType: "close_tabs",
        selectedTabs,
        error: "tabs_unavailable",
      });
      return false;
    }

    // Compare with the user selection rather than `requestedCount`.
    const affected = Math.max(
      0,
      result.requestedCount - result.failedTabs.length
    );
    this.#finalizeTabActionConfirmation({
      context,
      actionType: "close_tabs",
      selectedTabs,
      extraUpdateData: { operationIds: result.operationIds },
      resultInfo: {
        ...this.#summarizeTabActionOutcome(
          affected,
          selectedTabs.length,
          "some_tabs_failed_to_close"
        ),
        undoAvailable: !!result.operationIds.length,
      },
    });
    return true;
  }

  /**
   * Handler for tab selection cancellation
   *
   * @param {HandlerContext} context - Handler context
   * @returns {boolean} True if successful
   * @private
   */
  static #handleCancelTabSelection(context) {
    const { message, conversation, originalData, updateData, toolCallId } =
      context;

    // Use the provided reason or default to user_action for manual cancellations
    const reason = updateData?.reason || "user_action";
    const actionType = updateData?.actionType;

    this.#recordTabConfirmationResponse({
      context,
      actionType,
      response: "cancel",
      selected: 0,
      reason,
    });
    this.#recordConfirmedBrowserActionComplete({
      context,
      result: "cancelled",
      error: reason === "auto_cancel" ? "auto_cancel" : "",
    });

    this.clearTabKeys(toolCallId);

    conversation.updateToolUI(
      message,
      originalData,
      UI_TYPES.CANCELLED_COMPONENT
    );
    conversation.resolvePendingToolConfirmation(
      { description: "User cancelled the tab action. No action was taken." },
      toolCallId
    );
    return true;
  }

  /**
   * Handler for "Select all" / "Deselect all" on a tab confirmation. Only
   * records telemetry. The card keeps track of its own selection.
   *
   * @param {HandlerContext & {updateData: {allSelected: boolean}}} context -
   *   Handler context, where allSelected is true when the click selected
   *   every tab
   * @returns {boolean} True if the click was recorded
   * @private
   */
  static #handleToggleAllTabSelection(context) {
    const { message, conversation, updateData, mode } = context;
    const { toolUIData } = message;
    const action = this.#promptActionForUIData(toolUIData);
    if (!action) {
      return false;
    }

    lazy.ToolUITelemetry.recordBrowserActionPromptToggleAll({
      location: mode,
      chat_id: conversation.id,
      message_seq: conversation.messageCount,
      action,
      prompt_type: PROMPT_TYPES.SAFETY_CONFIRMATION,
      entrypoint: toolUIData.isResumeActivity ? "resume" : "browser_action",
      toggle: updateData?.allSelected
        ? TOGGLE_ALL_TYPES.SELECT_ALL
        : TOGGLE_ALL_TYPES.DESELECT_ALL,
      candidates: toolUIData.properties?.tabs?.length ?? 0,
    });
    return true;
  }

  /**
   * Handler for tab group confirmation
   *
   * @param {HandlerContext} context - Handler context
   * @returns {Promise<boolean>} True if successful
   * @private
   */
  static async #handleConfirmTabGroupSelection(context) {
    const { updateData, window, toolCallId } = context;
    const { selectedTabs = [], tabGroupLabel = "Tab Group" } = updateData ?? {};

    // The chat tab is one of the tabs the user confirmed, so it needs no
    // special handling here - see offerChatTab in ManageTabs.sys.mjs.
    const tokenToKey = this.#tabKeysByToolCall.get(toolCallId);
    const result = await this.createTabGroup({
      tabs: selectedTabs,
      tokenToKey,
      window,
      label: tabGroupLabel,
    });
    this.clearTabKeys(toolCallId);
    if (!result?.success) {
      // `null` when none of the tabs were verified and grouping was never
      // attempted, otherwise the grouping failed.
      this.#recordAbandonedTabConfirmation({
        context,
        actionType: "group_tabs",
        selectedTabs,
        error: result ? result.error || "group_failed" : "tabs_unavailable",
      });
      return false;
    }

    this.#finalizeTabActionConfirmation({
      context,
      actionType: "group_tabs",
      selectedTabs,
      extraUpdateData: {
        operationIds: [result.group.id],
        group: result.group,
      },
      resultInfo: {
        // The group holds exactly the confirmed tabs that could still be
        // grouped. Tabs closed between the card and the confirm are dropped
        // before grouping and never reach failedTabs, so counting those out
        // would report them as grouped.
        ...this.#summarizeTabActionOutcome(
          result.group.tabCount,
          selectedTabs.length,
          "some_tabs_could_not_be_grouped"
        ),
        undoAvailable: true,
      },
    });
    return true;
  }

  /**
   * Handler for open-and-group tab confirmation. Unlike group_tabs, the
   * selected tabs do not need to already be open - see
   * TabManagementService.resolveOrOpenTabs.
   *
   * @param {HandlerContext} context - Handler context
   * @returns {Promise<boolean>} True if successful
   * @private
   */
  static async #handleOpenAndGroupTabsSelection(context) {
    const { updateData, window, toolCallId, conversation } = context;
    const { selectedTabs = [], tabGroupLabel = "Tab Group" } = updateData ?? {};

    const originTab = this.findChatTab(conversation?.id);
    const result = await this.openOrGroupTabs({
      tabs: selectedTabs,
      window,
      label: tabGroupLabel,
      originTab,
    });
    this.clearTabKeys(toolCallId);
    if (!result?.success) {
      return false;
    }

    this.#finalizeTabActionConfirmation({
      context,
      actionType: "open_tabs",
      selectedTabs,
      extraUpdateData: {
        operationIds: result.group?.id ? [result.group.id] : [],
        group: result.group ?? null,
        mergedCount: result.mergedCount,
        switched: result.switched ?? false,
      },
    });

    return true;
  }

  /**
   * Handler for undoing tab group operation
   *
   * @param {HandlerContext} context - Handler context
   * @returns {Promise<boolean>} True if successful
   * @private
   */
  static async #handleUndoTabGroup(context) {
    const { updateData, message, conversation, window, originalData, mode } =
      context;
    const { operationIds = [], actionTimestamp } = updateData ?? {};
    const undoStartTime = Date.now();

    if (!operationIds.length) {
      lazy.console.error("ToolUI: No operationIds provided for undo tab group");
      return false;
    }

    const ungroupedTabs = [];
    for (const groupId of operationIds) {
      const result = await lazy.tabManagementService.ungroupTabs({
        groupId,
        window,
      });

      if (!result?.success) {
        lazy.console.error(
          "ToolUI: Failed to undo tab group:",
          result?.error || "Unknown error"
        );

        const timeDelta = actionTimestamp ? undoStartTime - actionTimestamp : 0;

        lazy.ToolUITelemetry.recordBrowserActionUndo({
          location: mode,
          chat_id: conversation.id,
          message_seq: conversation.messageCount,
          action: "group_tabs",
          tabs_restored: result?.ungroupedTabs?.length ?? 0,
          time_delta: Math.max(0, timeDelta),
          result: "error",
          error: result?.error || "ungroup_failed",
        });

        return false;
      }

      ungroupedTabs.push(...result.ungroupedTabs);
    }

    // Calculate time delta from when action completed to when undo was clicked
    const timeDelta = actionTimestamp ? undoStartTime - actionTimestamp : 0;

    // Record telemetry for browser action undo
    lazy.ToolUITelemetry.recordBrowserActionUndo({
      location: mode,
      chat_id: conversation.id,
      message_seq: conversation.messageCount,
      action: "group_tabs",
      tabs_restored: ungroupedTabs.length,
      time_delta: Math.max(0, timeDelta),
      result: "success",
      error: "",
    });

    // Update the UI to show the undo was successful
    const enhancedData = {
      ...originalData,
      updateData: {
        ...updateData,
        wasRestored: true,
        originalGroupedTabs: ungroupedTabs,
        actionType: "group_tabs", // Preserve the action type
      },
    };

    conversation.updateToolUI(message, enhancedData, UI_TYPES.AI_ACTION_RESULT);

    return true;
  }

  /**
   * Handler for undoing tab close operation
   *
   * @param {HandlerContext} context - Handler context
   * @returns {Promise<boolean>} True if successful
   * @private
   */
  static async #handleUndoTabClose(context) {
    const { updateData, message, conversation, originalData, mode } = context;
    const {
      operationIds = [],
      selectedTabs = [],
      actionTimestamp,
    } = updateData ?? {};
    const undoStartTime = Date.now();

    if (!operationIds.length) {
      lazy.console.error("ToolUI: No operationIds provided for undo");
      return false;
    }

    try {
      let restoredCount = 0;
      let requestedCount = 0;
      const failedTabs = [];
      for (const id of operationIds) {
        const result = await lazy.tabManagementService.restoreTabs({
          operationId: id,
        });
        restoredCount += result.restoredCount;
        requestedCount += result.requestedCount;
        if (result.failedTabs.length) {
          failedTabs.push(...result.failedTabs);
        }
      }

      lazy.console.log(`Restored ${restoredCount} of ${requestedCount} tabs`);

      // Calculate time delta from when action completed to when undo was clicked
      const timeDelta = actionTimestamp ? undoStartTime - actionTimestamp : 0;

      let undoResult = "success";
      let errorCode = "";

      if (failedTabs.length) {
        errorCode = "one_or_more_tabs_failed_to_restore";
        undoResult = restoredCount > 0 ? "partial_success" : "error";
      }

      // Record telemetry for browser action undo
      lazy.ToolUITelemetry.recordBrowserActionUndo({
        location: mode,
        chat_id: conversation.id,
        message_seq: conversation.messageCount,
        action: "close_tabs",
        tabs_restored: restoredCount,
        time_delta: Math.max(0, timeDelta),
        result: undoResult,
        error: errorCode,
      });

      // Update the UI to show the undo was successful
      const enhancedData = {
        ...originalData,
        updateData: {
          ...updateData,
          wasRestored: true,
          restoredCount,
          originalClosedTabs: selectedTabs,
          actionType: "close_tabs",
        },
      };

      conversation.updateToolUI(
        message,
        enhancedData,
        UI_TYPES.AI_ACTION_RESULT
      );
      return true;
    } catch (error) {
      // This will only catch catastrophic errors like invalid window
      // since TabManagementService has its own try/catch
      lazy.console.error("Failed to restore tabs:", error);

      // Calculate time delta for error case
      const timeDelta = actionTimestamp ? undoStartTime - actionTimestamp : 0;

      // Record telemetry for catastrophic failure
      lazy.ToolUITelemetry.recordBrowserActionUndo({
        location: mode,
        chat_id: conversation.id,
        message_seq: conversation.messageCount,
        action: "close_tabs",
        tabs_restored: 0,
        time_delta: Math.max(0, timeDelta),
        result: "error",
        error: error?.name || "invalid_window",
      });

      return false;
    }
  }

  /**
   * Handle retry prompt from the UI (clears the current tool UI)
   *
   * @param {object} context - The handler context
   * @returns {Promise<boolean>} True if successful
   * @private
   */
  static async #handleRetryPrompt(context) {
    const { message, conversation } = context;
    await conversation.updateToolUI(message, null, null);
    return true;
  }

  /**
   * Finds the last assistant text message in a conversation
   *
   * @param {Array} messages - The conversation messages array
   * @returns {object|null} The last assistant text message or null if not found
   * @private
   */
  static #findLastAssistantTextMessage(messages) {
    return (
      messages.findLast(
        message =>
          message.role === lazy.MESSAGE_ROLE.ASSISTANT &&
          message.content?.type === "text"
      ) ?? null
    );
  }

  /* ========================================================================
   * Handler Mapping and Public API
   * ======================================================================== */

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

  /**
   * Finds the original user prompt that led to the given assistant message
   * by traversing the message chain backwards using parentMessageId
   *
   * @param {Array} messages - All conversation messages
   * @param {object} assistantMessage - The assistant message with tool UI data
   * @returns {string|null} The original user prompt text, or null if not found
   */
  static findOriginalUserPrompt(messages, assistantMessage) {
    // Use parentMessageId to trace back to the user message
    let nextMessageId = assistantMessage.parentMessageId;
    // To prevent potential infinite loops, we set a maximum depth for traversal
    const maxDepth = 5;
    let depth = 0;

    // Follow the chain backwards to find the user message
    while (nextMessageId && depth < maxDepth) {
      const nextMessage = messages.find(m => m.id === nextMessageId);

      if (!nextMessage) {
        break;
      }

      if (
        nextMessage.role === lazy.MESSAGE_ROLE.USER &&
        nextMessage.content?.type === "text"
      ) {
        return nextMessage.content.body;
      }

      // Continue up the chain
      nextMessageId = nextMessage.parentMessageId;
      depth++;
    }

    return null;
  }

  /**
   * Resolves the browser action a confirmation card is for.
   *
   * @param {object} toolUIData - Tool UI data for the confirmation card
   * @returns {string | null} The action when the card is not a confirmation
   */
  static #promptActionForUIData(toolUIData) {
    const fallbackActionType = PROMPT_ACTION_BY_UI_TYPE[toolUIData.uiType];
    if (!fallbackActionType) {
      return null;
    }
    return toolUIData.properties?.actionType ?? fallbackActionType;
  }

  static handleUIDisplayTelemetry(toolUIData, telemetryData) {
    const actionType = this.#promptActionForUIData(toolUIData);
    if (!actionType) {
      return;
    }
    const tabs = toolUIData.properties?.tabs ?? [];
    const reason = this.#getConfirmationReason(tabs, actionType);

    lazy.ToolUITelemetry.recordBrowserActionPrompt({
      ...telemetryData,
      action: actionType,
      prompt_type: PROMPT_TYPES.SAFETY_CONFIRMATION,
      reason,
      candidates: tabs.length,
      preselected: 0, // we currently don't preselect any tabs
    });
  }

  /**
   * Opens the stored AITab page in the requested tab.
   *
   * @param {HandlerContext & {
   *   updateData: {openTarget: "current" | "new"}
   * }} context
   * @param {ChatMessage} context.message - Stored assistant message containing
   *   the viewer URL and card state. Used to validate the action and mark the
   *   card complete after opening.
   * @param {{openTarget: "current" | "new"}} context.updateData - Opens the
   *   page in the current tab or a new tab.
   * @param {ChatConversation} context.conversation - Conversation owning the
   *   card, used to update its state and record telemetry.
   * @param {ChromeWindow} context.window - Browser window hosting the chat.
   * @param {string} [context.mode] - Chat mode for telemetry.
   *
   * @returns {boolean} Whether opening the AITab was successful
   */
  static #handleOpenAITab({ message, updateData, conversation, window, mode }) {
    let openedLink = false;

    const { openTarget } = updateData ?? {};
    const viewerURL = message.toolUIData?.properties?.viewerURL;
    const parsedURL = URL.parse(viewerURL);
    if (
      message.toolUIData?.uiType !== UI_TYPES.AITAB ||
      !["choose", "complete"].includes(message.toolUIData.properties?.state) ||
      (openTarget !== "current" && openTarget !== "new") ||
      parsedURL?.protocol !== "about:" ||
      parsedURL.pathname !== "smartpage" ||
      !parsedURL.searchParams.get("page") ||
      !window?.gBrowser
    ) {
      return openedLink;
    }

    try {
      if (
        isSmartPageURL(parsedURL) &&
        lazy.URILoadingHelper.switchToTabHavingURI(window, viewerURL, false, {})
      ) {
        openedLink = true;
      } else {
        const { userContextId } =
          window.gBrowser.selectedBrowser.browsingContext.originAttributes;

        lazy.URILoadingHelper.openTrustedLinkIn(
          window,
          viewerURL,
          openTarget === "current" ? "current" : "tab",
          { userContextId, forceForeground: true, relatedToCurrent: true }
        );
        openedLink = true;
      }
    } catch (error) {
      lazy.console.error(`Error opening AITab URL: ${error}`);

      return openedLink;
    }

    if (openedLink) {
      Glean.smartWindow.linkClick.record({
        location: mode,
        chat_id: conversation.id,
        message_seq: conversation.messageCount,
      });
      Glean.smartWindow.aitabOpen.record({
        chat_id: conversation.id,
        open_target: openTarget,
        card_state: message.toolUIData.properties.state,
      });
      lazy.SmartWindowTelemetry.recordUriLoad();
      conversation.updateToolUI(
        message,
        { properties: { state: "complete", viewerURL } },
        UI_TYPES.AITAB
      );
    }

    return openedLink;
  }

  /**
   * Map of update type strings to their handler functions
   *
   * @private
   */
  static #UPDATE_TYPE_HANDLERS = {
    [UI_UPDATE_TYPES.OPEN_AITAB]: this.#handleOpenAITab.bind(this),
    [UI_UPDATE_TYPES.CONFIRMATION_TAB_SELECTION]:
      this.#handleConfirmationTabSelection.bind(this),
    [UI_UPDATE_TYPES.CANCEL_TAB_SELECTION]:
      this.#handleCancelTabSelection.bind(this),
    [UI_UPDATE_TYPES.CONFIRM_TAB_GROUP_SELECTION]:
      this.#handleConfirmTabGroupSelection.bind(this),
    [UI_UPDATE_TYPES.CONFIRM_OPEN_AND_GROUP_TABS_SELECTION]:
      this.#handleOpenAndGroupTabsSelection.bind(this),
    [UI_UPDATE_TYPES.TOGGLE_ALL_TAB_SELECTION]:
      this.#handleToggleAllTabSelection.bind(this),
    [UI_UPDATE_TYPES.UNDO_TAB_CLOSE]: this.#handleUndoTabClose.bind(this),
    [UI_UPDATE_TYPES.UNDO_TAB_GROUP]: this.#handleUndoTabGroup.bind(this),
    [UI_UPDATE_TYPES.RETRY_PROMPT]: this.#handleRetryPrompt.bind(this),
  };

  /**
   * Checks if the conversation has an active website confirmation and auto-cancels it.
   * This should be called when starting a new prompt to clean up pending confirmations.
   *
   * @param {object} conversation - The conversation object containing messages
   * @param {ChromeWindow} window - The browser window object
   * @param {string} mode - The mode of the AI Window (e.g., "sidebar", "popup")
   * @returns {Promise<boolean>} True if a confirmation was cancelled, false otherwise
   */
  static async autoCancelActiveConfirmation(conversation, window, mode) {
    if (!conversation?.messages?.length) {
      lazy.console.log("ToolUI: No conversation messages to check");
      return false;
    }

    const lastAssistantTextMessage = this.#findLastAssistantTextMessage(
      conversation.messages
    );

    // Early return if no confirmation to cancel (CONFIRMATION_UI_TYPES)
    const uiType = lastAssistantTextMessage?.toolUIData?.uiType;
    const isActiveConfirmation = CONFIRMATION_UI_TYPES.includes(uiType);

    if (!isActiveConfirmation) {
      lazy.console.log("ToolUI: No active confirmation to cancel");
      return false;
    }

    lazy.console.log(`ToolUI: Found active ${uiType} to cancel`);

    // Get the original user prompt from the existing toolUIData
    // This was already added when the website confirmation was created
    const originalUserPrompt =
      lastAssistantTextMessage.toolUIData.properties?.originalUserPrompt;

    const cancelData = {
      messageId: lastAssistantTextMessage.id,
      toolCallId: lastAssistantTextMessage.toolUIData.toolCallId,
      updateType: UI_UPDATE_TYPES.CANCEL_TAB_SELECTION,
      updateData: {
        reason: "auto_cancel",
        actionType: this.#promptActionForUIData(
          lastAssistantTextMessage.toolUIData
        ),
      },
    };

    // Set pending retry state BEFORE the await to avoid race condition
    // We'll clear it if cancellation fails - avoiding setting upstream methods to async for now
    if (originalUserPrompt) {
      conversation.pendingRetry = {
        originalUserPrompt,
        cancelledMessageId: lastAssistantTextMessage.id,
        cancelledToolCallId: lastAssistantTextMessage.toolUIData.toolCallId,
        cancelledUiType: lastAssistantTextMessage.toolUIData.uiType,
        timestamp: Date.now(),
      };
    }

    const cancelled = await this.handleUpdate(
      cancelData,
      conversation,
      window,
      mode
    );

    // Clear pendingRetry if cancellation failed
    if (!cancelled && originalUserPrompt) {
      conversation.pendingRetry = null;
    }

    return cancelled;
  }

  /**
   * Inject retry toolUIData into a message if there's a pending retry state
   *
   * @param {object} msg - The message object to potentially modify
   * @param {object} conversation - The conversation object that may have pendingRetry
   * @returns {boolean} True if retry toolUIData was injected, false otherwise
   */
  static injectRetryToolUIDataIfNeeded(msg, conversation) {
    lazy.console.log("ToolUI: Checking if retry injection needed", {
      hasPendingRetry: !!conversation?.pendingRetry,
      msgRole: msg?.role,
      contentType: msg?.content?.type,
    });

    if (
      !conversation?.pendingRetry ||
      msg?.role !== lazy.MESSAGE_ROLE.ASSISTANT ||
      msg?.content?.type !== "text"
    ) {
      return false;
    }

    lazy.console.log("ToolUI: Injecting retry component");

    // Create the retry toolUIData with the original prompt
    const retryToolUIData = {
      uiType: UI_TYPES.RETRY_COMPONENT,
      // Generate a unique synthetic ID for UI update handling (required by handleUpdate)
      toolCallId: `retry-${crypto.randomUUID()}`,
      properties: {
        originalUserPrompt: conversation.pendingRetry.originalUserPrompt,
        cancelledUiType: conversation.pendingRetry.cancelledUiType,
      },
    };

    // Inject the retry toolUIData into the message itself
    msg.toolUIData = retryToolUIData;

    // Clear the pending retry state
    conversation.pendingRetry = null;
    return true;
  }

  /**
   * Handle updates to tool UI components from user interactions
   *
   * @param {object} data - The update data
   * @param {string} data.messageId - ID of the message containing the tool UI
   * @param {string} data.toolCallId - ID of the specific tool call
   * @param {string} data.updateType - Type of update (confirmation, cancellation, etc.)
   * @param {ToolUpdateData} data.updateData - Additional data for the update
   * @param {object} conversation - The conversation object containing messages
   * @param {ChromeWindow} window - The browser window object
   * @param {string} [mode] - The mode of the AI Window (e.g., "sidebar", "popup") for context
   * @returns {Promise<boolean>} True if update was successful, false otherwise
   */
  static async handleUpdate(data, conversation, window, mode) {
    const { messageId, toolCallId, updateType, updateData } = data ?? {};

    if (!messageId || !toolCallId) {
      return false;
    }

    // Find the message in the conversation
    const message = conversation?.messages?.find(m => m.id === messageId);

    // Check if the message exists and has matching toolUIData
    if (message?.toolUIData?.toolCallId !== toolCallId) {
      return false;
    }

    // Get the handler for this update type
    const handler = this.#UPDATE_TYPE_HANDLERS[updateType];
    if (typeof handler !== "function") {
      lazy.console.error(`ToolUI: Unknown updateType "${updateType}"`);
      return false;
    }

    // Call the handler with all context, let it destructure what it needs
    return handler({
      message,
      toolCallId,
      updateData,
      conversation,
      window,
      originalData: data,
      mode,
    });
  }
}
