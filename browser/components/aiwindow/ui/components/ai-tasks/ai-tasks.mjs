/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/agent-monitor-item.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/monitors-display.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/monitor-icon.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-select.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-textarea.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/panel-list.mjs";

// Default constants - will be overridden by values from the actor
const DEFAULT_CONSTANTS = {
  TOTAL_NUM_MONITORS: 15,
  TOTAL_NUM_URLS_IN_MONITOR: 5,
  SCHEDULE_TYPES: {
    DAILY: "daily",
    WEEKLY: "weekly",
  },
  isMonitorRegionSupported: true,
  isBlocked: false,
  smartWindowSupportUrl: "https://support.mozilla.org/kb/smart-window",
};

// Actor action constants - must match the event names in SmartWindowTasksChild
const MONITOR_ACTIONS = {
  REQUEST_CONSTANTS: "RequestConstants",
  REQUEST_LIST_MONITORS: "RequestListMonitors",
  REQUEST_CREATE_MONITOR: "RequestCreateMonitor",
  REQUEST_DELETE_MONITOR: "RequestDeleteMonitor",
  REQUEST_UPDATE_MONITOR: "RequestUpdateMonitor",
  REQUEST_RUN_MONITOR: "RequestRunMonitor",
  REQUEST_PAUSE_MONITOR: "RequestPauseMonitor",
  REQUEST_OPEN_URL: "RequestOpenUrl",
};

const WEEKDAYS = [
  { value: 0, ftlId: "ai-tasks-alert-weekday-sunday" },
  { value: 1, ftlId: "ai-tasks-alert-weekday-monday" },
  { value: 2, ftlId: "ai-tasks-alert-weekday-tuesday" },
  { value: 3, ftlId: "ai-tasks-alert-weekday-wednesday" },
  { value: 4, ftlId: "ai-tasks-alert-weekday-thursday" },
  { value: 5, ftlId: "ai-tasks-alert-weekday-friday" },
  { value: 6, ftlId: "ai-tasks-alert-weekday-saturday" },
];

/**
 * AI Tasks component for displaying and managing tasks.
 *
 * The create form and the monitor cards are both `agent-monitor-item`, shared
 * with the chat. This component only owns the actor plumbing: it turns the
 * card's bubbling events into actor requests and reloads the list.
 *
 * @property {Array} monitors - Monitors loaded from the MonitorAgent
 * @property {boolean} dialogOpen - Whether the create dialog is open
 */
export class AITasks extends MozLitElement {
  static properties = {
    monitors: { type: Array },
    dialogOpen: { type: Boolean, state: true },
  };

  constructor() {
    super();
    // Use default constants initially, will be updated from actor
    this._constants = { ...DEFAULT_CONSTANTS };
    this.monitors = [];
    this.dialogOpen = false;
  }

  // ===== Lifecycle Methods =====

  async connectedCallback() {
    super.connectedCallback();
    // Try to initialize actor but don't block if it fails (e.g., in tests)
    this.#initializeActor().catch(error => {
      console.warn("Failed to initialize actor (expected in tests):", error);
    });
    // Load monitors async but don't block
    this.loadMonitors().catch(error => {
      console.warn("Failed to load monitors (expected in tests):", error);
    });

    // Bind event handlers for agent-monitor-item events
    this.boundHandleMonitorDelete = this.handleMonitorDelete.bind(this);
    this.boundHandleMonitorPause = this.handleMonitorPause.bind(this);
    this.boundHandleMonitorCheckNow = this.handleMonitorCheckNow.bind(this);
    this.boundHandleMonitorOpen = this.handleMonitorOpen.bind(this);
    this.boundHandleMonitorSubmit = this.handleMonitorSubmit.bind(this);
    this.boundHandleMonitorCancel = this.handleMonitorCancel.bind(this);
    this.boundHandleBlockedStateChanged =
      this.handleBlockedStateChanged.bind(this);

    window.addEventListener(
      "SmartWindowTasks:BlockedStateChanged",
      this.boundHandleBlockedStateChanged
    );

    // Listen for events bubbling up from the create dialog and from
    // monitors-display > agent-monitor-item
    this.addEventListener(
      "agent-monitor-item:delete",
      this.boundHandleMonitorDelete
    );
    this.addEventListener(
      "agent-monitor-item:pause",
      this.boundHandleMonitorPause
    );
    this.addEventListener(
      "agent-monitor-item:check-now",
      this.boundHandleMonitorCheckNow
    );
    this.addEventListener(
      "agent-monitor-item:submit",
      this.boundHandleMonitorSubmit
    );
    this.addEventListener(
      "AIChatContent:OpenLink",
      this.boundHandleMonitorOpen
    );
    this.addEventListener(
      "agent-monitor-item:cancel",
      this.boundHandleMonitorCancel
    );
  }

  /**
   * Initializes the actor communication and loads constants from the parent process.
   *
   * @private
   */
  async #initializeActor() {
    try {
      // Get constants from the parent process
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_CONSTANTS
      );
      if (result?.success && result.constants) {
        // Store constants on the instance to avoid shared mutable state
        this._constants = Object.freeze(result.constants);
        // _constants is not a reactive property, so request a render to
        // reflect the actor-provided values (e.g. region support).
        this.requestUpdate();
      }
    } catch (error) {
      console.error("Failed to initialize SmartWindowTasks actor:", error);
    }
  }

  /**
   * Dispatches an action to the SmartWindowTasks actor and waits for a response.
   * This follows the same event-based pattern as ai-chat-content but adds
   * response handling for operations that need to return data.
   *
   * @param {string} action - The action name to dispatch (e.g., "RequestListMonitors")
   * @param {object} detail - Optional data to send with the action
   * @returns {Promise<object>} Promise that resolves with the response from the parent actor
   * @private
   */
  #dispatchMonitorAction(action, detail = {}) {
    return new Promise((resolve, reject) => {
      const eventType = `SmartWindowTasks:${action}`;

      // Set up one-time listeners for response
      const handleResponse = event => {
        // Clean up the error listener to prevent memory leaks
        this.removeEventListener(`${eventType}:Error`, handleError);
        resolve(event.detail);
      };

      const handleError = event => {
        // Clean up the response listener to prevent memory leaks
        this.removeEventListener(`${eventType}:Response`, handleResponse);
        reject(new Error(event.detail?.error || "Operation failed"));
      };

      this.addEventListener(`${eventType}:Response`, handleResponse, {
        once: true,
      });
      this.addEventListener(`${eventType}:Error`, handleError, { once: true });

      // Dispatch the request event
      this.dispatchEvent(
        new CustomEvent(eventType, {
          bubbles: true,
          detail,
        })
      );
    });
  }

  disconnectedCallback() {
    super.disconnectedCallback();

    window.removeEventListener(
      "SmartWindowTasks:BlockedStateChanged",
      this.boundHandleBlockedStateChanged
    );

    // Remove event listeners
    this.removeEventListener(
      "agent-monitor-item:delete",
      this.boundHandleMonitorDelete
    );
    this.removeEventListener(
      "agent-monitor-item:pause",
      this.boundHandleMonitorPause
    );
    this.removeEventListener(
      "agent-monitor-item:check-now",
      this.boundHandleMonitorCheckNow
    );
    this.removeEventListener(
      "agent-monitor-item:submit",
      this.boundHandleMonitorSubmit
    );
    this.removeEventListener(
      "AIChatContent:OpenLink",
      this.boundHandleMonitorOpen
    );
    this.removeEventListener(
      "agent-monitor-item:cancel",
      this.boundHandleMonitorCancel
    );
  }

  // ===== Getters =====

  /**
   * Checks if the maximum number of active monitors has been reached.
   * Paused monitors do not count toward the limit.
   *
   * @returns {boolean} True if max active monitors reached, false otherwise
   */
  get isMaxMonitorsReached() {
    const activeCount = this.monitors.filter(monitor => monitor.enabled).length;
    return activeCount >= this._constants.TOTAL_NUM_MONITORS;
  }

  /**
   * Checks whether the current region supports monitors.
   *
   * @returns {boolean} True if monitors are supported in this region
   */
  get isMonitorRegionSupported() {
    return this._constants.isMonitorRegionSupported;
  }

  get #dialog() {
    return this.shadowRoot.querySelector("dialog");
  }

  /**
   * Refreshes the page when Smart Window is blocked or unblocked in AI
   * Controls.
   */
  async handleBlockedStateChanged() {
    await this.#initializeActor();
    if (this._constants.isBlocked) {
      this.closeDialog();
    }
    await this.loadMonitors();
  }

  // ===== Dialog Management =====

  openDialog() {
    // The card is only rendered while the dialog is open so every open starts
    // from a blank form.
    this.dialogOpen = true;
    this.#dialog?.showModal();
  }

  closeDialog() {
    this.dialogOpen = false;
    this.#dialog?.close();
  }

  // ===== Monitor Management =====

  /**
   * Handles delete monitor event from agent-monitor-item.
   *
   * @param {CustomEvent} event - Event containing monitor id
   */
  async handleMonitorDelete(event) {
    const { id } = event.detail;
    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_DELETE_MONITOR,
        { id }
      );
      // Only reload monitors if deletion actually occurred (not cancelled)
      if (result?.success && result?.deleted) {
        await this.loadMonitors(); // Refresh the list
      } else if (!result?.success) {
        console.error("Failed to delete monitor:", result?.error);
      }
      // If cancelled (result.success && result.cancelled), do nothing
    } catch (error) {
      console.error("Failed to delete monitor:", error);
    }
  }

  /**
   * Handles pause/unpause monitor event from agent-monitor-item.
   *
   * @param {CustomEvent} event - Event containing monitor id and paused state
   */
  async handleMonitorPause(event) {
    const { id, paused } = event.detail;
    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_PAUSE_MONITOR,
        { id, pause: paused }
      );
      if (result?.success) {
        await this.loadMonitors(); // Refresh the list
      } else {
        console.error("Failed to pause monitor:", result?.error);
      }
    } catch (error) {
      console.error("Failed to pause monitor:", error);
    }
  }

  /**
   * Handles check now event from agent-monitor-item.
   *
   * @param {CustomEvent} event - Event containing monitor id
   */
  async handleMonitorCheckNow(event) {
    const { id } = event.detail;

    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_RUN_MONITOR,
        { id }
      );
      if (result?.success) {
        await this.loadMonitors(); // Refresh the list
      } else {
        console.error("Failed to run monitor check:", result?.error);
      }
    } catch (error) {
      console.error("Failed to run monitor check:", error);
    }
  }

  /**
   * Handles the open event from a watched page chip in agent-monitor-item.
   *
   * @param {CustomEvent} event - Event containing the page url
   */
  async handleMonitorOpen(event) {
    event.stopPropagation();

    const { url } = event.detail;
    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_OPEN_URL,
        { url }
      );
      if (!result?.success) {
        console.error("Failed to open monitor URL:", result?.error);
      }
    } catch (error) {
      console.error("Failed to open monitor URL:", error);
    }
  }

  /**
   * Handles submit/update event from agent-monitor-item edit mode.
   * Handles the submit event from agent-monitor-item. The create dialog and the
   * edit form of an existing card share the event, so the mode picks the action.
   *
   * @param {CustomEvent} event - Event containing monitor data
   */
  handleMonitorSubmit(event) {
    if (event.detail?.mode === "create") {
      return this.createMonitor(event.detail);
    }
    return this.updateMonitor(event.detail);
  }

  /**
   * Closes the create dialog when its card is cancelled.
   */
  handleMonitorCancel() {
    this.closeDialog();
  }

  /**
   * Creates a monitor from the create card's form data.
   *
   * @param {object} detail - Submit event detail from agent-monitor-item
   */
  async createMonitor(detail) {
    const { monitorName, condition, watchUrls, schedule } = detail;
    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_CREATE_MONITOR,
        {
          prompt: condition,
          watchUrls,
          pageTitle: monitorName,
          schedule: this.#toMonitorSchedule(schedule),
          source: "about_page",
        }
      );

      if (!result?.success) {
        console.error("Failed to create monitor:", result?.error);
        return;
      }
    } catch (error) {
      console.error("Failed to create monitor:", error);
      return;
    }

    this.closeDialog();
    await this.loadMonitors();
  }

  /**
   * Applies an edit made in an existing monitor's card.
   *
   * @param {object} detail - Submit event detail from agent-monitor-item
   */
  async updateMonitor(detail) {
    const { id, monitorName, condition, watchUrls, schedule } = detail;
    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_UPDATE_MONITOR,
        {
          id,
          updates: {
            title: monitorName,
            monitorPrompt: condition,
            watchUrls,
            schedule: this.#toMonitorSchedule(schedule),
          },
        }
      );
      if (result?.success) {
        await this.loadMonitors(); // Refresh the list
      } else {
        console.error("Failed to update monitor:", result?.error);
      }
    } catch (error) {
      console.error("Failed to update monitor:", error);
    }
  }

  /**
   * Converts a schedule from the agent-monitor-item shape to the one the
   * MonitorAgent stores.
   *
   * @param {?object} schedule - { frequency, time: "HH:MM", weekday }
   * @returns {object|null} Schedule configuration object or null if not scheduled
   */
  #toMonitorSchedule(schedule) {
    const isScheduledFrequency = [
      this._constants.SCHEDULE_TYPES.DAILY,
      this._constants.SCHEDULE_TYPES.WEEKLY,
    ].includes(schedule?.frequency);

    if (!isScheduledFrequency || !schedule.time) {
      return null;
    }

    const [hour, minute] = schedule.time.split(":").map(Number);

    return {
      type: schedule.frequency,
      ...(schedule.frequency === this._constants.SCHEDULE_TYPES.WEEKLY && {
        weekday: Number(schedule.weekday),
      }),
      hour,
      minute,
    };
  }

  /**
   * Loads all existing monitors from the MonitorAgent.
   * Called on component connection and after creating new monitors.
   */
  async loadMonitors() {
    try {
      const result = await this.#dispatchMonitorAction(
        MONITOR_ACTIONS.REQUEST_LIST_MONITORS
      );
      if (result?.success) {
        this.monitors = result.monitors;
      } else {
        console.error("Failed to load monitors:", result?.error);
      }
    } catch (error) {
      console.error("Failed to load monitors:", error);
    }
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-tasks.css"
      />

      <dialog
        class="modal-wrapper"
        @close=${() => {
          this.dialogOpen = false;
        }}
      >
        ${this.dialogOpen
          ? html`<agent-monitor-item
              mode="create"
              .maxWatchUrls=${this._constants.TOTAL_NUM_URLS_IN_MONITOR}
            ></agent-monitor-item>`
          : nothing}
      </dialog>

      ${this._constants.isBlocked || !this.isMonitorRegionSupported
        ? html`
            <div class="unavailable-wrapper">
              <div class="unavailable-container">
                <h2
                  data-l10n-id=${this._constants.isBlocked
                    ? "ai-tasks-blocked-title"
                    : "ai-tasks-no-monitors-title"}
                ></h2>
                <p data-l10n-id="ai-tasks-no-monitors-message">
                  <a
                    href=${this._constants.smartWindowSupportUrl}
                    data-l10n-name="smart-window-link"
                    target="_blank"
                  ></a>
                </p>
              </div>
            </div>
          `
        : html`<div class="page-wrapper">
            <div class="page-container">
              <div class="header">
                <div class="title-container">
                  <monitor-icon></monitor-icon>
                  <h2 data-l10n-id="ai-tasks-page-title"></h2>
                </div>

                <moz-button
                  iconSrc="chrome://global/skin/icons/plus.svg"
                  type="primary"
                  class="add-task-button"
                  data-l10n-id="ai-tasks-add-alert-button"
                  ?disabled=${this.isMaxMonitorsReached}
                  @click=${() => this.openDialog()}
                ></moz-button>
              </div>
              <!-- Monitors display component -->

              <monitors-display
                .monitors=${this.monitors}
                .scheduleTypes=${this._constants.SCHEDULE_TYPES}
                .weekdays=${WEEKDAYS}
                .canResume=${!this.isMaxMonitorsReached}
                .activeLimit=${this._constants.TOTAL_NUM_MONITORS}
              ></monitors-display>
            </div>
          </div>`}
    `;
  }
}

customElements.define("ai-tasks", AITasks);
