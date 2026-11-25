/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @import { HandlerContext, TabActionCompletion, TabSelectionData } from "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
 */

import { UI_TYPES } from "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs";

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
 * ToolUI telemetry calls
 */
export const ToolUITelemetry = {
  /**
   * Records telemetry when a browser action prompt is displayed
   *
   * @param {object} data - Telemetry data
   * @param {string} data.location - The location/mode of the AI Window
   * @param {string} data.chat_id - The conversation ID
   * @param {number} data.message_seq - The conversation message count
   * @param {string} data.action - The browser action being prompted for
   * @param {string} data.prompt_type - The type of prompt being shown
   * @param {"browser_action" | "resume"} data.entrypoint - What opened the prompt
   * @param {string} data.reason - The reason for the prompt
   * @param {number} data.candidates - Number of candidates being acted upon
   * @param {number} data.preselected - Number of preselected items
   */
  recordBrowserActionPrompt(data) {
    Glean.smartWindow.browserActionPrompt.record(data);
  },

  /**
   * Records telemetry when a user responds to a browser action prompt
   *
   * @param {object} data - Telemetry data
   * @param {string} data.location - The location/mode of the AI Window
   * @param {string} data.chat_id - The conversation ID
   * @param {number} data.message_seq - The conversation message count
   * @param {string} data.action - The browser action being performed
   * @param {string} data.prompt_type - The type of prompt that was responded to
   * @param {"browser_action" | "resume"} data.entrypoint - What opened the prompt
   * @param {string} data.response - The user's response (confirm/cancel)
   * @param {number} data.selected - Number of items selected
   * @param {string} data.reason - The reason for the action
   */
  recordBrowserActionPromptResponse(data) {
    Glean.smartWindow.browserActionPromptResponse.record(data);
  },

  /**
   * Records telemetry when a user clicks "Select all" or "Deselect all" on a
   * browser action prompt
   *
   * @param {object} data - Telemetry data
   * @param {string} data.location - The location/mode of the AI Window
   * @param {string} data.chat_id - The conversation ID
   * @param {number} data.message_seq - The conversation message count
   * @param {string} data.action - The browser action being prompted for
   * @param {string} data.prompt_type - The type of prompt being shown
   * @param {"browser_action" | "resume"} data.entrypoint - What opened the prompt
   * @param {"select_all" | "deselect_all"} data.toggle - The button clicked
   * @param {number} data.candidates - Number of tabs shown on the prompt
   */
  recordBrowserActionPromptToggleAll(data) {
    Glean.smartWindow.browserActionPromptToggleAll.record(data);
  },

  /**
   * Records telemetry when a user undoes a browser action
   *
   * @param {object} data - Telemetry data
   * @param {string} data.location - The location/mode of the AI Window
   * @param {string} data.chat_id - The conversation ID
   * @param {number} data.message_seq - The conversation message count
   * @param {string} data.action - The browser action being undone
   * @param {number} data.tabs_restored - Number of tabs that were restored
   * @param {number} data.time_delta - Time elapsed since the original action
   * @param {string} data.result - The result of the undo operation
   * @param {string} data.error - Error code if the operation failed
   */
  recordBrowserActionUndo(data) {
    Glean.smartWindow.browserActionUndo.record(data);
  },

  /**
   * Records telemetry when a browser action request is submitted.
   * In practice, when the manage_tabs tool is invoked by the model.
   *
   * @param {object} data - Telemetry data
   * @param {string} data.location - The location/mode of the AI Window
   * @param {string} data.chat_id - The conversation ID
   * @param {number} data.message_seq - The conversation message count
   * @param {string} data.model - Identifier of the model that invoked the tool
   * @param {string} data.prompt_version - Major version of the chat prompt
   * @param {string} data.submit_type - How the request was submitted (button, enter, etc.)
   * @param {"tab_mention" | "description" | "unsupported"} data.trigger - How the
   *   action was triggered
   * @param {number} data.tabs_open - Number of tabs open at submit time
   * @param {number} data.mentions - Number of tab mentions in the request
   */
  recordBrowserActionSubmit(data) {
    Glean.smartWindow.browserActionSubmit.record(data);
  },

  /**
   * Records telemetry when a browser action initiated by the model has
   * completed. Successfully, with an error, or because the user cancelled it.
   *
   * @param {object} data - Telemetry data
   * @param {string} data.location - The location/mode of the AI Window
   * @param {string} data.chat_id - The conversation ID
   * @param {number} data.message_seq - The conversation message count
   * @param {string} data.model - Identifier of the model that invoked the tool
   * @param {string} data.prompt_version - Major version of the chat prompt
   * @param {"tab_mention" | "description" | "unsupported"} data.trigger - How the
   *   action was triggered
   * @param {string} data.result - Outcome: success, partial_success, no_match,
   *   cancelled, or error
   * @param {number} data.tabs_affected - Number of tabs actually closed
   * @param {boolean} data.undo_available - Whether an undo path was available after the action
   * @param {string} data.error - Error code if the action was unsuccessful
   */
  recordBrowserActionComplete(data) {
    Glean.smartWindow.browserActionComplete.record(data);
  },

  /**
   * Maps tab-action counts to the `result` value recorded on
   * `browser_action_complete`.
   *
   * @param {number} affected - Tabs the action succeeded on
   * @param {number} requested - Tabs the action was asked to act on
   * @returns {"success" | "partial_success" | "error"}
   */
  browserActionResult(affected, requested) {
    const failedCount = Math.max(0, requested - affected);
    if (!failedCount) {
      return "success";
    }
    return affected ? "partial_success" : "error";
  },

  /**
   * Returns why a tab confirmation is shown, for `browser_action_prompt`.
   *
   * @param {Array<object>} tabs - Tabs offered on the confirmation card
   * @param {string} actionType - Action the confirmation is for
   * @returns {string}
   */
  getConfirmationReason(tabs, actionType) {
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
  },

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
  recordTabConfirmationResponse({
    context,
    actionType,
    response,
    selected,
    reason = "user_action",
  }) {
    const { conversation, mode, message } = context;

    this.recordBrowserActionPromptResponse({
      location: mode,
      chat_id: conversation.id,
      message_seq: conversation.messageCount,
      action: actionType,
      prompt_type: PROMPT_TYPES.SAFETY_CONFIRMATION,
      entrypoint: message?.toolUIData?.isResumeActivity
        ? "resume"
        : "browser_action",
      response,
      selected,
      reason,
    });
  },

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
  recordConfirmedBrowserActionComplete({
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

    this.recordBrowserActionComplete({
      ...baseTelemetryInfo,
      result,
      tabs_affected: tabsAffected,
      undo_available: undoAvailable,
      error,
    });
  },

  /**
   * Returns the shared `browserActionResult` outcome.
   *
   * @param {number} affected - Tabs successfully acted on
   * @param {number} requested - Tabs the user selected
   * @param {string} failureMessage - Error code to use when any tab failed
   * @returns {{result: TabActionCompletion, tabsAffected: number,
   *   error: string}}
   */
  summarizeTabActionOutcome(affected, requested, failureMessage) {
    return {
      result: this.browserActionResult(affected, requested),
      tabsAffected: affected,
      error: affected < requested ? failureMessage : "",
    };
  },

  /**
   * Records accepted confirmations for tabs that are not available anymore.
   *
   * @param {object} options
   * @param {HandlerContext} options.context - Handler context
   * @param {string} options.actionType - Action the confirmation was for
   * @param {Array<TabSelectionData>} options.selectedTabs - Tabs the user selected
   * @param {string} options.error - Error code explaining the failure
   */
  recordAbandonedTabConfirmation({ context, actionType, selectedTabs, error }) {
    this.recordTabConfirmationResponse({
      context,
      actionType,
      response: "confirm",
      selected: selectedTabs.length,
    });
    this.recordConfirmedBrowserActionComplete({
      context,
      result: "error",
      error,
    });
  },

  /**
   * Resolves the browser action a confirmation card is for.
   *
   * @param {object} toolUIData - Tool UI data for the confirmation card
   * @returns {string | null} The action when the card is not a confirmation
   */
  promptActionForUIData(toolUIData) {
    const fallbackActionType = PROMPT_ACTION_BY_UI_TYPE[toolUIData.uiType];
    if (!fallbackActionType) {
      return null;
    }
    return toolUIData.properties?.actionType ?? fallbackActionType;
  },

  /**
   * Records `browser_action_prompt` when a tab confirmation card is shown.
   *
   * @param {object} toolUIData - Tool UI data for the card
   * @param {object} telemetryData - Location, chat and message fields
   */
  recordPromptDisplay(toolUIData, telemetryData) {
    const actionType = this.promptActionForUIData(toolUIData);
    if (!actionType) {
      return;
    }
    const tabs = toolUIData.properties?.tabs ?? [];
    const reason = this.getConfirmationReason(tabs, actionType);

    this.recordBrowserActionPrompt({
      ...telemetryData,
      action: actionType,
      prompt_type: PROMPT_TYPES.SAFETY_CONFIRMATION,
      entrypoint: toolUIData.isResumeActivity ? "resume" : "browser_action",
      reason,
      candidates: tabs.length,
      preselected: 0, // we currently don't preselect any tabs
    });
  },

  /**
   * Records a click on "Select all" or "Deselect all" on a tab confirmation.
   *
   * @param {HandlerContext & {updateData: {allSelected: boolean}}} context -
   *   Handler context, where allSelected is true when the click selected
   *   every tab
   * @returns {boolean} True if the click was recorded
   */
  recordPromptToggleAll(context) {
    const { message, conversation, updateData, mode } = context;
    const { toolUIData } = message;
    const action = this.promptActionForUIData(toolUIData);
    if (!action) {
      return false;
    }

    this.recordBrowserActionPromptToggleAll({
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
  },
};
