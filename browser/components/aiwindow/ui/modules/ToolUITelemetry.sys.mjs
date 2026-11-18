/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

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
};
