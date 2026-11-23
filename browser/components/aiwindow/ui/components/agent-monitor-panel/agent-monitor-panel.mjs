/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/agent-monitor-item.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/monitor-status-chip.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
});

// The panel is a summary rather than the whole list, which lives behind
// "Manage and view all tasks". New matches take these slots first: they are
// what pulled the user to the panel.
const MAX_VISIBLE_ROWS = 5;

/**
 * Contents of the "Tasks" toolbar panel: the monitors the user is watching and,
 * once they ask for one, the create form.
 *
 * Like agent-monitor-item this is host-agnostic. Monitor data comes in via
 * properties and actions leave as bubbling CustomEvents, so the host owns the
 * data and the unsubmitted draft. Only which view is showing is local state.
 *
 * The host owns which view is showing, because its panel header names the view
 * and offers the way back out of it. Asking for a different view is therefore an
 * event like any other action, and 'view' only says what to render.
 *
 * Dispatches:
 *  - agent-monitor-panel:create-task
 *  - agent-monitor-panel:manage-tasks
 *  - agent-monitor-panel:open-task  (detail: { id }; a row was activated. Only
 *    fires for a task that watches at least one page.)
 *  - agent-monitor-item:*  (re-dispatched from the create form, see that
 *    component; the host handles :submit, :cancel and :draft-change)
 *
 * @property {object[]} monitors - Monitors in the order they should be listed,
 *   each formatted by MonitorUIUtils.formatMonitorForDisplay().
 * @property {number} maxMonitors - How many monitors the user may have.
 * @property {?object} agent - What the create form starts from, e.g. the page
 *   the user is on. See agent-monitor-item's 'agent'.
 * @property {?object} draft - In-progress create form state, owned by the host.
 * @property {"list"|"create"} view - Which view to render.
 * @property {?string} justCreatedId - This Id is used to trigger an animation
 * to show the user the newly created monitor task
 * @property {string[]} attentionIds - Ids of monitors that newly matched since
 *   the user last opened the panel. They are listed together under a "New
 *   matches" section above the rest, and fill the panel's limited rows first.
 */
export class AgentMonitorPanel extends MozLitElement {
  static properties = {
    monitors: { type: Array },
    maxMonitors: { type: Number },
    agent: { type: Object },
    draft: { type: Object },
    view: { type: String, reflect: true },
    justCreatedId: { type: String },
    attentionIds: { type: Array },
  };

  constructor() {
    super();
    this.monitors = [];
    this.maxMonitors = 0;
    this.agent = null;
    this.draft = null;
    this.view = "list";
    this.justCreatedId = null;
    this.attentionIds = [];
  }

  updated(changed) {
    super.updated(changed);
    if (changed.has("view") && this.view === "create") {
      this.#focusCreateForm();
    }
    // Animate the trips between the list and the create form
    if (
      changed.has("view") &&
      changed.get("view") === "create" &&
      !changed.has("justCreatedId")
    ) {
      this.shadowRoot
        .querySelector(".monitor-list-view")
        ?.classList.add("slide-in-back");
    }
    if (changed.has("view") && changed.get("view") === "list") {
      this.shadowRoot
        .querySelector("agent-monitor-item")
        ?.classList.add("slide-in-forward");
    }
  }

  async #focusCreateForm() {
    const form = this.shadowRoot.querySelector("agent-monitor-item");
    await form?.updateComplete;
    // The view can change again while the card renders.
    if (this.view === "create") {
      form?.focusName();
    }
  }

  #dispatch(type, detail) {
    this.dispatchEvent(
      new CustomEvent(type, { detail, bubbles: true, composed: true })
    );
  }

  /**
   * @param {object} monitor
   * @returns {object|null} The most recent history entry, if the monitor has run.
   */
  #lastRun(monitor) {
    return monitor.history?.length ? monitor.history[0] : null;
  }

  #renderSchedule(monitor) {
    const schedule = lazy.MonitorUIUtils.getScheduleL10n(monitor.schedule);
    if (!schedule) {
      return nothing;
    }
    return html`<span
      class="monitor-row-meta"
      data-l10n-id=${schedule.id}
      data-l10n-args=${JSON.stringify(schedule.args)}
    ></span>`;
  }

  #renderResult(monitor, isNewMatch) {
    const lastRun = this.#lastRun(monitor);
    if (!lastRun || lastRun.status === "running") {
      return nothing;
    }
    if (lastRun.status === "error") {
      return html`<span
        class="monitor-row-result could-not-check"
        data-l10n-id="smartwindow-monitor-panel-result-could-not-check"
      ></span>`;
    }
    if (lastRun.conditionMet) {
      return html`<span class="monitor-row-result match">
        ${isNewMatch
          ? html`<span class="monitor-row-match-dot" aria-hidden="true"></span>`
          : nothing}
        <span data-l10n-id="smartwindow-monitor-panel-result-match"></span>
      </span>`;
    }
    return html`<span
      class="monitor-row-result no-match"
      data-l10n-id="smartwindow-monitor-panel-result-no-match"
    ></span>`;
  }

  #onRowClick(monitor) {
    // Activating a row opens the pages the task watches, so a task with none
    // has nothing to open. The row stays focusable so it can still be read.
    if (!monitor.watchUrls?.length) {
      return;
    }
    this.#dispatch("agent-monitor-panel:open-task", { id: monitor.id });
  }

  #renderRow(monitor, isNewMatch) {
    return html`
      <button
        type="button"
        class="monitor-row"
        aria-disabled=${monitor.watchUrls?.length ? nothing : "true"}
        ?data-just-created=${monitor.id === this.justCreatedId}
        @click=${() => this.#onRowClick(monitor)}
      >
        <monitor-status-chip
          kind=${monitor.status?.kind ?? nothing}
        ></monitor-status-chip>
        <div class="monitor-row-text">
          <span class="monitor-row-title">${monitor.monitorName}</span>
          ${this.#renderSchedule(monitor)}
        </div>
        ${this.#renderResult(monitor, isNewMatch)}
      </button>
    `;
  }

  #renderSection(l10nId, monitors, isNewMatch) {
    if (!monitors.length) {
      return nothing;
    }
    return html`
      <div class="monitor-section-label" data-l10n-id=${l10nId}></div>
      <div class="monitor-rows">
        ${monitors.map(monitor => this.#renderRow(monitor, isNewMatch))}
      </div>
    `;
  }

  #renderList() {
    const attention = new Set(this.attentionIds ?? []);
    const newMatches = this.monitors
      .filter(monitor => attention.has(monitor.id))
      .slice(0, MAX_VISIBLE_ROWS);
    const recent = this.monitors
      .filter(monitor => !attention.has(monitor.id))
      .slice(0, MAX_VISIBLE_ROWS - newMatches.length);
    return html`
      <div class="monitor-list-view">
        ${this.monitors.length
          ? html`
              ${this.#renderSection(
                "smartwindow-monitor-panel-new-matches",
                newMatches,
                true
              )}
              ${this.#renderSection(
                "smartwindow-monitor-panel-watching",
                recent,
                false
              )}
            `
          : html`<div class="monitor-empty">
              <div class="monitor-empty-kit" aria-hidden="true"></div>
              <h2
                class="monitor-empty-title"
                data-l10n-id="smartwindow-monitor-panel-empty-title"
              ></h2>
              <span
                class="monitor-empty-description"
                data-l10n-id="smartwindow-monitor-panel-empty-description"
              ></span>
            </div>`}
        ${this.#renderFooter()}
      </div>
    `;
  }

  #renderFooter() {
    // Paused monitors don't count toward the limit.
    const activeCount = this.monitors.filter(monitor => monitor.enabled).length;
    const atLimit = activeCount >= this.maxMonitors;
    return html`
      <div class="monitor-footer">
        <button
          type="button"
          class="monitor-footer-row"
          ?disabled=${atLimit}
          @click=${() => this.#dispatch("agent-monitor-panel:create-task", {})}
        >
          <img
            class="monitor-footer-icon"
            src="chrome://global/skin/icons/plus.svg"
            alt=""
          />
          <span data-l10n-id="smartwindow-monitor-panel-create"></span>
          <span
            class="monitor-footer-count"
            data-l10n-id="smartwindow-monitor-panel-count"
            data-l10n-args=${JSON.stringify({
              used: activeCount,
              max: this.maxMonitors,
            })}
          ></span>
          <span class="monitor-footer-chevron" aria-hidden="true"></span>
        </button>
        <button
          type="button"
          class="monitor-footer-row"
          @click=${() => this.#dispatch("agent-monitor-panel:manage-tasks", {})}
        >
          <img
            class="monitor-footer-icon"
            src="chrome://browser/content/aiwindow/assets/agent-watch.svg"
            alt=""
          />
          <span data-l10n-id="smartwindow-monitor-panel-manage"></span>
        </button>
      </div>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/agent-monitor-panel.css"
      />
      ${this.view === "create"
        ? html`<agent-monitor-item
            mode="create"
            .agent=${this.agent}
            .draft=${this.draft}
            .selfContained=${false}
          ></agent-monitor-item>`
        : this.#renderList()}
    `;
  }
}

customElements.define("agent-monitor-panel", AgentMonitorPanel);
