/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  nothing,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/agent-monitor-item.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-tasks-group.mjs";

/**
 * Component for displaying monitor items using agent-monitor-item cards.
 *
 * @property {Array} monitors - Array of monitor objects to display
 * @property {object} scheduleTypes - Schedule type constants from parent
 * @property {Array} weekdays - Weekday definitions from parent (required for localization)
 * @property {boolean} canResume - Whether a paused monitor may be resumed, ie
 *   whether there is room under the active-monitor limit for one more
 * @property {number} activeLimit - Maximum number of active monitors
 */
export class MonitorsDisplay extends MozLitElement {
  static properties = {
    monitors: { type: Array },
    scheduleTypes: { type: Object },
    weekdays: { type: Array },
    canResume: { type: Boolean },
    activeLimit: { type: Number },
  };

  constructor() {
    super();
    this.monitors = [];
    this.scheduleTypes = {};
    this.weekdays = [];
    this.canResume = true;
    this.activeLimit = 0;
  }

  buildMonitorStatus(monitor) {
    // Return the status with kind, let agent-monitor-item handle the localization
    const monitorStatus = monitor.enabled
      ? { kind: "watching" }
      : { kind: "paused" };

    return monitorStatus;
  }

  /**
   * Transforms a monitor object to match the agent-monitor-item format.
   *
   * @param {object} monitor - The monitor object from the database
   * @returns {object} Transformed agent object for agent-monitor-item
   */
  transformMonitorToAgent(monitor) {
    const monitorStatus = this.buildMonitorStatus(monitor);

    return {
      id: monitor.id,
      monitorName: monitor.title || "Untitled Monitor",
      url: monitor.watchUrls?.[0] || "",
      watchUrls: monitor.watchUrls || [],
      watchUrlTitles: monitor.watchUrlTitles || {},
      condition: monitor.monitorPrompt || "",
      status: monitorStatus,
      history: (monitor.history || []).slice().reverse(),
      // Pass the schedule data directly - agent-monitor-item will format it using FTL strings
      schedule: monitor.schedule
        ? {
            frequency: monitor.schedule.type,
            time: `${(monitor.schedule.hour ?? 0)
              .toString()
              .padStart(2, "0")}:${(monitor.schedule.minute ?? 0)
              .toString()
              .padStart(2, "0")}`,
            weekday: monitor.schedule.weekday?.toString(),
          }
        : null,
    };
  }

  monitorTemplate(monitor) {
    return html`
      <agent-monitor-item
        .showLastResult=${true}
        .agent=${this.transformMonitorToAgent(monitor)}
        .canResume=${this.canResume}
        mode="display"
      ></agent-monitor-item>
    `;
  }

  /**
   * @param {string} headingL10nId - Fluent ID of the group heading
   * @param {string} countL10nId - Fluent ID of the count next to the heading
   * @param {Array} monitors - Monitors in the group; an empty group isn't shown
   * @param {object} [countArgs] - Extra Fluent args for the count
   */
  groupTemplate(headingL10nId, countL10nId, monitors, countArgs = {}) {
    if (!monitors.length) {
      return nothing;
    }
    return html`
      <ai-tasks-group
        class="monitors-group"
        .headingL10nId=${headingL10nId}
        .countL10nId=${countL10nId}
        .countL10nArgs=${{ count: monitors.length, ...countArgs }}
      >
        ${repeat(
          monitors,
          monitor => monitor.id,
          monitor => this.monitorTemplate(monitor)
        )}
      </ai-tasks-group>
    `;
  }

  render() {
    const activeMonitors = this.monitors.filter(monitor => monitor.enabled);
    const pausedMonitors = this.monitors.filter(monitor => !monitor.enabled);

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/monitors-display.css"
      />

      <div class="monitors-section">
        ${this.monitors.length
          ? html`
              <p data-l10n-id="ai-task-page-description"></p>

              ${this.groupTemplate(
                "ai-tasks-group-monitors-active",
                "ai-tasks-group-count-of-limit",
                activeMonitors,
                { limit: this.activeLimit }
              )}
              ${this.groupTemplate(
                "ai-tasks-group-monitors-paused",
                "ai-tasks-group-count",
                pausedMonitors
              )}
            `
          : html` <section class="no-monitors-wrapper">
              <div class="no-monitors-container">
                <h2 data-l10n-id="ai-task-page-no-alerts-title"></h2>
                <p data-l10n-id="ai-task-page-no-alerts"></p>
              </div>
            </section>`}
      </div>
    `;
  }
}

customElements.define("monitors-display", MonitorsDisplay);
