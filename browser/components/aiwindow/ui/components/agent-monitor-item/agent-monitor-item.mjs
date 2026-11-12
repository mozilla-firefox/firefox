/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-input-text.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-input-url.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-textarea.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-select.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/monitor-icon.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/monitor-status-chip.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-chip.mjs";
import { monitorErrorL10nId } from "chrome://browser/content/aiwindow/components/monitor-error-copy.mjs";

// What a finished check can say about the condition. A run that failed reports
// neither met nor not-met: it never got to compare anything.
const RESULT_STATES = Object.freeze({
  MET: "met",
  NOT_MET: "not-met",
  COULD_NOT_CHECK: "could-not-check",
});

const RESULT_BADGE_L10N_IDS = Object.freeze({
  [RESULT_STATES.MET]: "ai-tasks-alert-condition-met",
  [RESULT_STATES.NOT_MET]: "ai-tasks-alert-condition-not-met",
  [RESULT_STATES.COULD_NOT_CHECK]: "ai-tasks-alert-condition-could-not-check",
});

const LAST_RESULT_L10N_IDS = Object.freeze({
  [RESULT_STATES.MET]: "ai-tasks-alert-last-result-met",
  [RESULT_STATES.NOT_MET]: "ai-tasks-alert-last-result-not-met",
  [RESULT_STATES.COULD_NOT_CHECK]: "ai-tasks-alert-last-result-could-not-check",
});

const SCHEDULE_TYPES = Object.freeze({
  DAILY: "daily",
  WEEKLY: "weekly",
});

const SCHEDULE_ICON = "chrome://browser/skin/calendar-24.svg";
const TIME_ICON = "chrome://browser/skin/history-20.svg";
const DEFAULT_MAX_WATCH_URLS = 5;

// How long to coalesce typing before mirroring the form to the host
const DRAFT_PERSIST_DELAY_MS = 250;

// Poll rather than wait in one go, since a long setTimeout can
// overshoot its deadline across a suspend or clock change.
const MAX_REENABLE_DELAY_MS = 60_000;

// Check times offered by the create card in 30-minute increments
const TIME_OPTIONS = Array.from({ length: 48 }, (_, i) => {
  const hour24 = Math.floor(i / 2);
  const minute = i % 2 ? 30 : 0;

  // Create a date object with the specific time for localization
  const timeDate = new Date();
  timeDate.setHours(hour24, minute, 0, 0);

  // Use toLocaleTimeString for locale-appropriate formatting
  const label = timeDate.toLocaleTimeString(undefined, {
    hour: "numeric",
    minute: "2-digit",
    hour12: true,
  });

  return {
    value: `${String(hour24).padStart(2, "0")}:${String(minute).padStart(2, "0")}`,
    label,
  };
});

/**
 * Rounds the user's local time up to the nearest TIME_OPTIONS slot, giving the
 * time input a more useful default than always starting at 9:00. Seconds are
 * ignored, so 2:00:10 PM still yields 2:00 PM while 2:01 PM yields 2:30 PM.
 *
 * @param {Date} [now] - The time to round up from
 * @returns {string} An "HH:MM" value matching one of TIME_OPTIONS
 */
function nextTimeOption(now = new Date()) {
  const minutesSinceMidnight = now.getHours() * 60 + now.getMinutes();
  const slot = Math.ceil(minutesSinceMidnight / 30);

  // Wrap past the final 11:30 PM option back to midnight.
  if (slot >= TIME_OPTIONS.length) {
    return TIME_OPTIONS[0].value;
  }

  return TIME_OPTIONS[slot].value;
}

// Indexed by the weekday values used by the scheduler (0 = Sunday)
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
 * A single monitor card:
 * It has three modes and is host-agnostic - monitor data comes
 * in via the 'agent' property and every action is a bubbling CustomEvent for
 * the host to handle. The host also decides the collapse affordance (chat uses
 * the chevron; the page can click-to-expand), so the component only exposes
 * 'expanded'/'editing' state
 *
 * Modes:
 *  - "display": collapsed shows the head  with a chevron and
 *    expanded reveals the value, condition, actions and change history.
 *  - "create": the full form used when setting up a new monitor.
 *
 * Dispatches:
 *  - agent-monitor-item:toggle       detail: { expanded }
 *  - agent-monitor-item:edit-toggle  detail: { editing }
 *  - agent-monitor-item:submit       detail: { mode, id, monitorName, condition, watchUrls, schedule }
 *  - agent-monitor-item:draft-change detail: { draft: MonitorDraft|null }
 *  - agent-monitor-item:resolve-titles detail: { urls } - Asks for the page
 *    titles of form pages that have none. Whoever answers dispatches
 *    agent-monitor-item:titles-resolved, detail: { titles }, on this card.
 *  - agent-monitor-item:cancel
 *  - agent-monitor-item:delete       detail: { id }
 *  - agent-monitor-item:pause        detail: { id, paused }
 *  - agent-monitor-item:check-now    detail: { id }
 *
 * @property {Agent} agent - Monitor data:
 *  {
 *    id: string,
 *    monitorName: string,
 *    url: string,
 *    watchUrls?: string[],
 *    watchUrlTitles?: Record<string, string>, // url -> page title, resolved by
 *                               // the host from Places; chips fall back to the
 *                               // hostname for URLs
 *    faviconText?: string,      // 1-2 char fallback favicon glyph
 *    faviconColor?: string,     // fallback favicon background
 *    condition?: string,        // e.g. "the price drops below $270"
 *    conditionPresets?: string[],
 *    status?: { label: string, kind?: "watching"|"paused" },
 *    cadence?: string,
 *    history?: Array<{ when: string, oldValue?: string, newValue?: string,
 *                      note?: string, flag?: string, low?: boolean }>,
 *  }
 * @property {?MonitorDraft} draft - In-progress form state to restore over the
 *  values seeded from 'agent'. The card only renders it, the host owns it:
 *  every edit is mirrored back out via 'agent-monitor-item:draft-change' so an
 *  unsubmitted form survives the card being torn down and rebuilt.
 *  {
 *    editing?: boolean,        // reopen the edit form the draft belongs to
 *    monitorName?: string,
 *    condition?: string,
 *    watchUrls?: string[],
 *    pendingUrl?: string,
 *    schedule?: { frequency: string, time: string, weekday: number },
 *  }
 * @property {"display"|"create"} mode - Which card layout to render
 * @property {boolean} expanded - Whether the display card is expanded
 * @property {boolean} editing - Whether the editable condition field is shown
 * @property {boolean} showLastResult - Whether to show the last check result chip (defaults to false)
 * @property {number} maxWatchUrls - How many pages one monitor may watch
 * @property {boolean} selfContained - Whether the card stands on its own
 *  (defaults to true). A self-contained card draws its own frame and states its
 *  own title. Hosts that already frame and title it - a panel with a header, say
 *  - set this to false and get just the contents.
 * @property {boolean} canResume - Whether a paused monitor may be resumed
 *  (defaults to true). Hosts that cap how many monitors run at once set this to
 *  false once the cap is reached, so Resume reads as unavailable rather than
 *  failing after the user presses it. Pausing is never blocked.
 */
export class AgentMonitorItem extends MozLitElement {
  static properties = {
    agent: { type: Object },
    draft: { type: Object },
    mode: { type: String, reflect: true },
    expanded: { type: Boolean, reflect: true },
    editing: { type: Boolean, reflect: true },
    showLastResult: { type: Boolean },
    maxWatchUrls: { type: Number },
    selfContained: {
      type: Boolean,
      reflect: true,
      attribute: "self-contained",
    },
    canResume: { type: Boolean },
    checkFrequency: { type: String, state: true },
    scheduleTime: { type: String, state: true },
    scheduleWeekday: { type: Number, state: true },
    alertDescription: { type: String, state: true },
    pageUrls: { type: Array, state: true },
    pendingUrl: { type: String, state: true },
    pendingUrlError: { type: Object, state: true },
    fieldErrors: { type: Object, state: true },
    resolvedTitles: { type: Object, state: true },
  };

  constructor() {
    super();
    this.agent = {};
    this.draft = null;
    this.mode = "display";
    this.expanded = false;
    this.editing = false;
    this.showLastResult = false;
    this.maxWatchUrls = DEFAULT_MAX_WATCH_URLS;
    this.selfContained = true;
    this.canResume = true;
    this.checkFrequency = SCHEDULE_TYPES.DAILY;
    this.scheduleTime = nextTimeOption();
    this.scheduleWeekday = 1;
    this.alertDescription = "";
    this.pageUrls = [];
    this.pendingUrl = "";
    this.pendingUrlError = null;
    this.fieldErrors = { name: null, condition: null, pages: null };
    this.resolvedTitles = {};
    this.#draftName = null;
    this.addEventListener("agent-monitor-item:titles-resolved", event => {
      this.resolvedTitles = { ...this.resolvedTitles, ...event.detail?.titles };
    });
  }

  #draftName;
  #draftPersistTimer = null;
  #requestedTitleUrls = new Set();
  #checkLimitTimer = null;

  willUpdate(changed) {
    if (changed.has("agent")) {
      this.#seedFromAgent();
      this.#armCheckLimitTimer();
    }

    if (changed.has("agent") || changed.has("draft")) {
      this.#applyDraft();
    }
  }

  updated(changed) {
    if (
      changed.has("pageUrls") ||
      changed.has("mode") ||
      changed.has("editing")
    ) {
      this.#requestMissingTitles();
    }
  }

  #requestMissingTitles() {
    if (this.mode !== "create" && !this.editing) {
      return;
    }
    const urls = this.pageUrls.filter(
      url => !this.#titleFor(url) && !this.#requestedTitleUrls.has(url)
    );
    if (!urls.length) {
      return;
    }
    urls.forEach(url => this.#requestedTitleUrls.add(url));
    this.#dispatch("agent-monitor-item:resolve-titles", { urls });
  }

  #titleFor(url) {
    return this.resolvedTitles[url] || this.agent?.watchUrlTitles?.[url];
  }

  connectedCallback() {
    super.connectedCallback();
    this.#armCheckLimitTimer();
  }

  disconnectedCallback() {
    if (this.#draftPersistTimer) {
      this.#flushDraft();
    }
    this.#clearCheckLimitTimer();
    super.disconnectedCallback();
  }

  /**
   * The service's refusal that checks are waiting out, or null when they are
   * available.
   *
   * @returns {?{until: number, reason: string, errorCode: string}}
   */
  get #activeCheckLimit() {
    const limit = this.agent?.checkLimit;
    return limit?.until > Date.now() ? limit : null;
  }

  #clearCheckLimitTimer() {
    if (this.#checkLimitTimer) {
      clearTimeout(this.#checkLimitTimer);
      this.#checkLimitTimer = null;
    }
  }

  // render again once the wait is over, so the button comes back on its own
  #armCheckLimitTimer() {
    this.#clearCheckLimitTimer();
    const until = this.agent?.checkLimit?.until;
    if (!this.isConnected || !(until > Date.now())) {
      return;
    }
    const delay = Math.min(until - Date.now(), MAX_REENABLE_DELAY_MS);
    this.#checkLimitTimer = setTimeout(() => {
      this.#checkLimitTimer = null;
      if (this.agent?.checkLimit?.until > Date.now()) {
        this.#armCheckLimitTimer();
      } else {
        this.requestUpdate();
      }
    }, delay);
  }

  #seedFromAgent() {
    this.#draftName = null;
    this.#requestedTitleUrls.clear();

    const { watchUrls, url, condition, expanded } = this.agent ?? {};
    let seededUrls = [];
    if (watchUrls?.length) {
      seededUrls = watchUrls;
    } else if (url) {
      seededUrls = [url];
    }
    this.pageUrls = seededUrls.filter(u => u?.trim().length);
    this.alertDescription = condition ?? "";
    this.pendingUrl = "";
    this.pendingUrlError = null;
    this.fieldErrors = { name: null, condition: null, pages: null };

    // If the agent data includes an expanded state, apply it
    if (expanded !== undefined) {
      this.expanded = expanded;
    }

    // Seed the schedule fields from an existing monitor so edit mode reflects
    // its current scheduled
    const schedule = this.agent?.schedule;
    if (schedule) {
      this.checkFrequency = schedule.frequency ?? this.checkFrequency;
      this.scheduleTime = schedule.time ?? this.scheduleTime;
      this.scheduleWeekday = schedule.weekday
        ? Number(schedule.weekday)
        : this.scheduleWeekday;
    }
  }

  /**
   * Restores an unsubmitted form over the values seeded from 'agent'.
   */
  #applyDraft() {
    if (!this.draft) {
      return;
    }
    const { editing, monitorName, condition, watchUrls, pendingUrl, schedule } =
      this.draft;

    // A draft only outlives the card while an edit is unsubmitted, so reopen
    // the form the user was in the middle of. The edit affordances live in the
    // expanded body, so the card has to come back expanded to show them.
    if (editing) {
      this.editing = true;
      this.expanded = true;
    }
    if (monitorName !== undefined) {
      this.#draftName = monitorName;
    }
    if (condition !== undefined) {
      this.alertDescription = condition;
    }
    if (watchUrls) {
      this.pageUrls = [...watchUrls];
    }
    if (pendingUrl !== undefined) {
      this.pendingUrl = pendingUrl;
    }
    if (schedule?.frequency) {
      this.checkFrequency = schedule.frequency;
    }
    if (schedule?.time) {
      this.scheduleTime = schedule.time;
    }
    if (schedule?.weekday !== undefined) {
      this.scheduleWeekday = Number(schedule.weekday);
    }
  }

  /**
   * Mirrors the in-progress form out to the host.
   *
   * @param {object} [options]
   * @param {boolean} [options.debounce] - Coalesce rapid edits, for typing
   */
  #persistDraft({ debounce = false } = {}) {
    if (this.mode !== "create" && !this.editing) {
      return;
    }
    this.#clearDraftTimer();
    if (!debounce) {
      this.#flushDraft();
      return;
    }
    this.#draftPersistTimer = setTimeout(
      () => this.#flushDraft(),
      DRAFT_PERSIST_DELAY_MS
    );
  }

  #flushDraft() {
    this.#clearDraftTimer();
    this.#dispatch("agent-monitor-item:draft-change", {
      draft: {
        editing: this.editing,
        monitorName: this.#monitorName,
        condition: this.alertDescription,
        watchUrls: [...this.pageUrls],
        pendingUrl: this.pendingUrl,
        schedule: {
          frequency: this.checkFrequency,
          time: this.scheduleTime,
          weekday: this.scheduleWeekday,
        },
      },
    });
  }

  #discardDraft() {
    this.#clearDraftTimer();
    this.#dispatch("agent-monitor-item:draft-change", { draft: null });
  }

  #clearDraftTimer() {
    if (this.#draftPersistTimer) {
      clearTimeout(this.#draftPersistTimer);
      this.#draftPersistTimer = null;
    }
  }

  #dispatch(type, detail) {
    this.dispatchEvent(
      new CustomEvent(type, { detail, bubbles: true, composed: true })
    );
  }

  get #monitorName() {
    return this.#draftName ?? this.agent?.monitorName ?? "";
  }

  get #isAtMaxUrls() {
    return this.pageUrls.length >= this.maxWatchUrls;
  }

  focusName() {
    this.shadowRoot?.querySelector(".monitor-name-input")?.focus();
  }

  /**
   * moz-select renders its own popover-based list as soon as any option has an
   * icon, and a popover goes in the document's top layer - which is the wrong
   * place when the host is a XUL panel, because the panel is its own widget. So
   * a hosted card drops the option icons to keep the native <select>, whose
   * dropdown the platform positions correctly over a panel.
   *
   * @param {string} icon
   * @returns {string|nothing}
   */
  #optionIcon(icon) {
    return this.selfContained ? icon : nothing;
  }

  // Fields the form checks on submit, in the order errors are surfaced and
  // focused. Returns the keys that failed.
  #validateForm() {
    // A URL left in the input has to be committed before submit, so a valid one
    // isn't silently dropped and an invalid one sets pendingUrlError. Once the
    // cap is reached there's no room for it, so it's ignored instead.
    if (this.#isAtMaxUrls) {
      this.pendingUrlError = null;
    } else if (this.pendingUrl.trim()) {
      this.#addUrl();
    }
    const errors = { name: null, condition: null, pages: null };
    if (this.mode === "create" && !this.#monitorName.trim()) {
      errors.name = { id: "ai-tasks-alert-error-name-required" };
    }
    if (!this.alertDescription?.trim()) {
      errors.condition = { id: "ai-tasks-alert-error-condition-required" };
    }
    if (!this.pageUrls.length) {
      errors.pages = { id: "ai-tasks-alert-error-no-pages" };
    }
    this.fieldErrors = errors;
    const invalid = ["name", "condition", "pages"].filter(key => errors[key]);

    if (this.pendingUrlError && !invalid.includes("pages")) {
      invalid.push("pages");
    }
    return invalid;
  }

  #clearFieldError(key) {
    if (this.fieldErrors[key]) {
      this.fieldErrors = { ...this.fieldErrors, [key]: null };
    }
  }

  #focusField(key) {
    const selector = {
      name: ".monitor-name-input",
      condition: ".monitor-condition-input",
      pages: ".page-url-input",
    }[key];
    this.shadowRoot.querySelector(selector)?.focus();
  }

  #onNameInput(event) {
    this.#draftName = event.target.value;
    this.#clearFieldError("name");
    // Typing coalesces, a change (blur) flushes right away
    this.#persistDraft({ debounce: event.type === "input" });
  }

  #onCardClick(e) {
    if (e.target.closest("button, moz-button")) {
      return;
    }
    this.#onToggle(e);
  }

  #onToggle() {
    this.expanded = !this.expanded;
    this.#dispatch("agent-monitor-item:toggle", { expanded: this.expanded });
  }

  #onEditToggle() {
    this.editing = !this.editing;
    // Opening the form is itself worth remembering, so an edit session that
    // hasn't been typed in yet survives too. Leaving discards the edit.
    if (this.editing) {
      this.#persistDraft();
    } else {
      this.#discardDraft();
    }
    this.#dispatch("agent-monitor-item:edit-toggle", { editing: this.editing });
  }

  #onConditionInput(event) {
    this.alertDescription = event.target.value;
    this.#clearFieldError("condition");
    this.#persistDraft({ debounce: event.type === "input" });
  }

  #onPresetClick(preset) {
    this.alertDescription = preset;
    this.#clearFieldError("condition");
    this.#persistDraft();
  }

  // Normalize a user-entered address to a watchable http(s) URL. A value with
  // no scheme (e.g. "cnn.com") is watched over https so the user doesn't have
  // to type it. Returns the normalized URL as entered.
  #normalizeUrl(url) {
    const value = url.trim();
    if (!value) {
      return "";
    }
    const candidate = value.includes("://") ? value : `https://${value}`;
    try {
      const { protocol, hostname } = new URL(candidate);
      if ((protocol === "http:" || protocol === "https:") && hostname) {
        return new URL(candidate).href;
      }
    } catch {}
    return "";
  }

  // Whether two watch URLs point at the same page, comparing canonical forms so
  // "cnn.com", "CNN.com" and "https://cnn.com/" count as one.
  #isSameUrl(a, b) {
    try {
      return new URL(a).href === new URL(b).href;
    } catch {
      return a === b;
    }
  }

  // Returns the { id } Fluent error descriptor and the url to store.
  #validateAndNormalizeURL(url) {
    const normalized = this.#normalizeUrl(url);
    if (!normalized) {
      return {
        valid: false,
        error: { id: "ai-tasks-alert-error-invalid-url" },
        normalized: "",
      };
    }
    return { valid: true, error: null, normalized };
  }

  #addUrl() {
    if (!this.pendingUrl.trim()) {
      return;
    }
    const { valid, error, normalized } = this.#validateAndNormalizeURL(
      this.pendingUrl
    );
    if (!valid) {
      this.pendingUrlError = error;
      return;
    }
    if (this.pageUrls.some(existing => this.#isSameUrl(existing, normalized))) {
      this.pendingUrlError = { id: "ai-tasks-alert-error-duplicate-url" };
      return;
    }
    if (this.#isAtMaxUrls) {
      this.pendingUrlError = {
        id: "ai-tasks-alert-error-max-urls",
        args: { maxUrls: this.maxWatchUrls },
      };
      return;
    }
    this.pageUrls = [...this.pageUrls, normalized];
    this.pendingUrl = "";
    this.pendingUrlError = null;
    this.#clearFieldError("pages");
    this.#persistDraft();
  }

  async #handleAddUrl() {
    this.#addUrl();
    if (this.#isAtMaxUrls) {
      await this.updateComplete;
      // Give a fallback focus to the pages field if the add button is removed
      this.#focusField("pages");
    }
  }

  #removeUrl(url) {
    this.pageUrls = this.pageUrls.filter(u => u !== url);
    this.#persistDraft();
  }

  #displayUrl(url) {
    try {
      return new URL(url).hostname;
    } catch {
      return url;
    }
  }

  #onPendingUrlInput(event) {
    this.pendingUrl = event.target.value;
    if (this.pendingUrlError) {
      this.pendingUrlError = null;
    }
    this.#persistDraft({ debounce: true });
  }

  #onPendingUrlKeydown(event) {
    if (event.key !== "Enter") {
      return;
    }
    event.preventDefault();
    this.#addUrl();
  }

  #onCancel() {
    // Cancelling drops the whole card, and with it the draft the host holds,
    // so this only has to stop a coalesced edit from landing after that
    this.#clearDraftTimer();
    this.#dispatch("agent-monitor-item:cancel", {});
  }

  async #onSubmit() {
    const invalidFields = this.#validateForm();
    if (invalidFields.length) {
      await this.updateComplete;
      this.#focusField(invalidFields[0]);
      return;
    }
    // The host drops the draft as it commits the form, so make sure a coalesced
    // edit can't land after that and resurrect it
    this.#clearDraftTimer();
    const isCreateMode = this.mode === "create";
    this.#dispatch("agent-monitor-item:submit", {
      mode: this.mode,
      id: this.agent?.id,
      monitorName: this.#monitorName,
      condition: this.alertDescription.trim(),
      watchUrls: [...this.pageUrls],
      schedule: {
        frequency: this.checkFrequency,
        time: this.scheduleTime,
        weekday: this.scheduleWeekday,
      },
      autoExpandAndCheck: isCreateMode, // Signal to expand and check after creation
    });
    // Exit edit mode after saving
    if (this.editing) {
      this.editing = false;
      this.#dispatch("agent-monitor-item:edit-toggle", { editing: false });
    }
  }

  #renderStatusChip() {
    return html`<monitor-status-chip
      kind=${this.agent?.status?.kind ?? nothing}
    ></monitor-status-chip>`;
  }

  #renderLastCheckedCondition() {
    // Get the most recent history item (first in array) to show its condition
    // status. It goes through the same transform as the history rows so a
    // failed run reads the same status in both places.
    const [mostRecentItem] = this.agent?.history ?? [];
    const normalizedItem = mostRecentItem
      ? this.#transformHistoryItem(mostRecentItem)
      : null;

    if (!normalizedItem) {
      return nothing;
    }

    return html`<span
      class="last-result ${mostRecentItem.conditionMet ? "match" : "not-match"}"
      data-l10n-id=${LAST_RESULT_L10N_IDS[normalizedItem.resultState]}
    ></span>`;
  }

  #renderFieldError(error) {
    return error
      ? html`<div
          class="error-message"
          data-l10n-id=${error.id}
          data-l10n-args=${error.args ? JSON.stringify(error.args) : nothing}
        ></div>`
      : nothing;
  }

  #renderConditionField() {
    const presets = this.agent?.conditionPresets ?? [];
    return html`
      <div class="field">
        <moz-textarea
          class="monitor-condition-input"
          data-l10n-id="ai-tasks-alert-alert"
          data-l10n-attrs="placeholder,label,description"
          required
          ?invalid=${!!this.fieldErrors.condition}
          .value=${this.alertDescription}
          @input=${this.#onConditionInput}
          @change=${this.#onConditionInput}
        ></moz-textarea>
        ${this.#renderFieldError(this.fieldErrors.condition)}
        ${presets.length
          ? html`<div class="chip-row">
              ${presets.map(
                preset =>
                  html`<moz-button
                    class="chip ${preset === this.alertDescription
                      ? "selected"
                      : ""}"
                    label=${preset}
                    @click=${() => this.#onPresetClick(preset)}
                  ></moz-button>`
              )}
            </div>`
          : nothing}
      </div>
    `;
  }

  #renderPagesField() {
    return html`
      <div class="field">
        <div class="pages-container">
          <div class="page-input-row">
            <moz-input-url
              class="form-input page-url-input"
              required
              ?invalid=${!!this.pendingUrlError || !!this.fieldErrors.pages}
              data-l10n-id="ai-tasks-alert-pages"
              data-l10n-attrs="placeholder,label"
              data-l10n-args=${JSON.stringify({
                maxPages: this.maxWatchUrls,
              })}
              .value=${this.pendingUrl}
              @input=${this.#onPendingUrlInput}
              @keydown=${this.#onPendingUrlKeydown}
            ></moz-input-url>
            ${this.#isAtMaxUrls
              ? nothing
              : html`<moz-button
                  size="small"
                  type="icon ghost"
                  class="add-page-btn"
                  iconsrc="chrome://global/skin/icons/plus.svg"
                  data-l10n-id="ai-tasks-alert-add-url"
                  data-l10n-attrs="aria-label"
                  @click=${() => this.#handleAddUrl()}
                ></moz-button>`}
          </div>
          ${this.#renderFieldError(
            this.pendingUrlError ?? this.fieldErrors.pages
          )}
          ${this.pageUrls.length
            ? html`<div class="page-pills-row">
                ${this.pageUrls.map(
                  url =>
                    html`<smartwindow-chip
                      type="source"
                      removable
                      label=${this.#titleFor(url) || this.#displayUrl(url)}
                      .iconSrc=${`page-icon:${url}`}
                      title=${url}
                      @smartwindow-chip:remove=${() => this.#removeUrl(url)}
                    ></smartwindow-chip>`
                )}
              </div>`
            : nothing}
        </div>
      </div>
    `;
  }

  #transformHistoryItem(item) {
    // Only process items with valid timestamps
    if (!item.checkedAt) {
      return null;
    }

    // Format the timestamp
    const date = new Date(item.checkedAt);
    const displayTime =
      date.toLocaleDateString(undefined, {
        month: "short",
        day: "numeric",
        year: "numeric",
      }) +
      " - " +
      date.toLocaleTimeString(undefined, {
        hour: "numeric",
        minute: "2-digit",
        hour12: true,
      });

    // A failed run has no comparison to report, so the badge says so and the
    // note explains the failure. resultExplanation holds the raw error message
    // for a failed run, so it is deliberately not shown to the user.
    if (item.status === "error") {
      return {
        when: displayTime,
        resultState: RESULT_STATES.COULD_NOT_CHECK,
        note: "",
        noteL10nId: monitorErrorL10nId(item.errorCode),
        status: item.status,
        low: true,
      };
    }

    // Handle conditionMet cases
    if (item.conditionMet) {
      return {
        when: displayTime,
        resultState: RESULT_STATES.MET,
        note: item.resultExplanation || "",
        status: item.status,
        low: false,
      };
    }

    // Handle condition not met
    return {
      when: displayTime,
      resultState: RESULT_STATES.NOT_MET,
      note: item.resultExplanation || "",
      noteL10nId: item.resultExplanation
        ? null
        : "smartwindow-agent-monitor-history-no-match",
      status: item.status,
      low: true,
    };
  }

  #renderHistory() {
    const historyItems = this.agent?.history ?? [];
    if (!historyItems.length) {
      return nothing;
    }

    return html`
      <div
        class="section-toggle"
        data-l10n-id="ai-tasks-alert-change-history"
      ></div>
      <div class="history">
        ${historyItems.map(item => {
          const normalizedItem = this.#transformHistoryItem(item);

          // Skip items without valid timestamps
          if (!normalizedItem) {
            return nothing;
          }

          return html`<div class="history-item">
            <span class="when">${normalizedItem.when}</span>
            <span
              class="condition-badge ${normalizedItem.resultState}"
              data-l10n-id=${RESULT_BADGE_L10N_IDS[normalizedItem.resultState]}
            ></span>
            ${(() => {
              if (normalizedItem.noteL10nId) {
                return html`<span
                  data-l10n-id=${normalizedItem.noteL10nId}
                ></span>`;
              }
              if (normalizedItem.note) {
                return html`<span>${normalizedItem.note}</span>`;
              }
              return nothing;
            })()}
          </div>`;
        })}
      </div>
      <div class="history-note">
        <p data-l10n-id="ai-tasks-alert-change-history-description"></p>
      </div>
    `;
  }

  #onFrequencyChange(event) {
    this.checkFrequency = event.target.value;
    this.#persistDraft();
  }

  #onScheduleTimeChange(event) {
    this.scheduleTime = event.target.value;
    this.#persistDraft();
  }

  #onWeekdayChange(event) {
    this.scheduleWeekday = Number(event.target.value);
    this.#persistDraft();
  }

  #renderScheduleSummary() {
    const schedule = this.agent?.schedule;
    if (!schedule) {
      return nothing;
    }

    // Convert HH:MM string to a Date object for Fluent formatting
    // Use a fixed date to ensure consistent formatting
    const [hours, minutes] = schedule.time.split(":").map(Number);
    const timeDate = new Date();
    timeDate.setHours(hours, minutes, 0, 0);

    if (schedule.frequency === SCHEDULE_TYPES.WEEKLY) {
      // Map weekday index to the specific Fluent string ID
      const weekdayFluent = [
        "ai-tasks-alert-schedule-weekly-sunday",
        "ai-tasks-alert-schedule-weekly-monday",
        "ai-tasks-alert-schedule-weekly-tuesday",
        "ai-tasks-alert-schedule-weekly-wednesday",
        "ai-tasks-alert-schedule-weekly-thursday",
        "ai-tasks-alert-schedule-weekly-friday",
        "ai-tasks-alert-schedule-weekly-saturday",
      ];

      const fluentId = weekdayFluent[schedule.weekday];
      if (fluentId) {
        return html`<div class="monitor-row">
          <span
            class="val"
            data-l10n-id=${fluentId}
            data-l10n-args=${JSON.stringify({
              time: timeDate.getTime(),
            })}
          ></span>
        </div>`;
      }
    }

    return html`
      <span
        class="val"
        data-l10n-id="ai-tasks-alert-schedule-daily-at"
        data-l10n-args=${JSON.stringify({ time: timeDate.getTime() })}
      ></span>
    `;
  }

  #renderTimeField() {
    return html`<div class="form-section-half">
      <label
        class="form-label"
        data-l10n-id="ai-tasks-alert-time-label"
      ></label>

      <moz-select
        class="form-select"
        .value=${this.scheduleTime}
        @change=${this.#onScheduleTimeChange}
      >
        ${TIME_OPTIONS.map(
          opt =>
            html`<moz-option
              value=${opt.value}
              label=${opt.label}
              iconsrc=${this.#optionIcon(TIME_ICON)}
            ></moz-option>`
        )}
      </moz-select>
    </div>`;
  }

  #renderScheduler() {
    return html`
      <div class="form-row">
        <div class="schedule-container">
          <div class="form-section-half">
            <label
              class="form-label"
              data-l10n-id="ai-tasks-alert-check-label"
            ></label>
            <moz-select
              class="form-select"
              .value=${this.checkFrequency}
              @change=${this.#onFrequencyChange}
            >
              <moz-option
                value=${SCHEDULE_TYPES.DAILY}
                data-l10n-id="ai-tasks-alert-schedule-daily"
                iconsrc=${this.#optionIcon(SCHEDULE_ICON)}
              ></moz-option>
              <moz-option
                value=${SCHEDULE_TYPES.WEEKLY}
                data-l10n-id="ai-tasks-alert-schedule-weekly"
                iconsrc=${this.#optionIcon(SCHEDULE_ICON)}
              ></moz-option>
            </moz-select>
          </div>
          ${this.checkFrequency === SCHEDULE_TYPES.WEEKLY
            ? html`<div class="form-section-half">
                <label
                  class="form-label"
                  data-l10n-id="ai-tasks-alert-day-label"
                ></label>
                <moz-select
                  class="form-select"
                  value=${this.scheduleWeekday}
                  @change=${this.#onWeekdayChange}
                >
                  ${WEEKDAYS.map(
                    day =>
                      html`<moz-option
                        value=${day.value}
                        data-l10n-id=${day.ftlId}
                        iconsrc=${this.#optionIcon(SCHEDULE_ICON)}
                      ></moz-option>`
                  )}
                </moz-select>
              </div>`
            : nothing}
        </div>
        ${this.#renderTimeField()}
      </div>
    `;
  }

  #renderCreate() {
    return html`
      <div class="monitor-card">
        ${this.selfContained
          ? html`<div class="title-container">
              <monitor-icon size="small"></monitor-icon>
              <h2
                class="monitor-card-state-title"
                data-l10n-id="ai-tasks-alert-modal-title"
              ></h2>
            </div>`
          : nothing}
        <div class="monitor-card-head">
          <div class="monitor-name-field">
            <moz-input-text
              class="monitor-name-input"
              data-l10n-id="ai-tasks-alert-name"
              data-l10n-attrs="label"
              required
              ?invalid=${!!this.fieldErrors.name}
              .value=${this.#monitorName}
              @input=${this.#onNameInput}
              @change=${this.#onNameInput}
            ></moz-input-text>
            ${this.#renderFieldError(this.fieldErrors.name)}
          </div>
        </div>
        ${this.#renderConditionField()} ${this.#renderPagesField()}
        ${this.#renderScheduler()}

        <p
          class="required-note"
          data-l10n-id="ai-tasks-alert-required-note"
        ></p>

        <div class="monitor-card-actions">
          <span class="spacer"></span>
          <moz-button
            id="cancel-create-button"
            type="default"
            data-l10n-id="ai-tasks-alert-cancel-button"
            data-l10n-attrs="label"
            @click=${this.#onCancel}
          ></moz-button>
          <moz-button
            type="primary"
            data-l10n-id="ai-tasks-alert-create-button"
            data-l10n-attrs="label"
            @click=${this.#onSubmit}
          ></moz-button>
        </div>
      </div>
    `;
  }

  #renderDisplay() {
    const agent = this.agent ?? {};
    return html`
      <div class="monitor-card chatcard" @click=${this.#onCardClick}>
        <div class="monitor-card-head">
          <div class="monitor-card-head-left">
            ${this.#renderStatusChip()}
            <span class="monitor-card-title"
              ><span class="monitor-card-name">${agent.monitorName}</span></span
            >
          </div>
          <div class="monitor-card-head-right">
            ${this.showLastResult
              ? this.#renderLastCheckedCondition()
              : nothing}
            <button
              type="button"
              class="chev"
              aria-expanded=${this.expanded}
              data-l10n-id="ai-tasks-alert-show-details"
              data-l10n-attrs="aria-label"
              @click=${this.#onToggle}
            ></button>
          </div>
        </div>
        ${this.expanded ? this.#renderExpand() : nothing}
      </div>
    `;
  }

  #renderExpand() {
    const agent = this.agent ?? {};
    const checkLimit = this.#activeCheckLimit;
    return html`
      <div class="watch-expand" @click=${e => e.stopPropagation()}>
        ${this.editing
          ? html`${this.#renderConditionField()} ${this.#renderPagesField()}
            ${this.#renderScheduler()}`
          : html`<div class="task-section">
                <div
                  class="task-header"
                  data-l10n-id="ai-tasks-alert-the-alert"
                ></div>
                <div class="task-content">${agent.condition}</div>
              </div>
              ${agent.watchUrls?.length
                ? html`<div class="url-section">
                    <div
                      class="section-toggle"
                      data-l10n-id="ai-tasks-alert-on-this-page"
                    ></div>
                    <div class="url-chips">
                      ${agent.watchUrls.map(
                        url =>
                          html`<smartwindow-chip
                            type="source"
                            label=${agent.watchUrlTitles?.[url] ??
                            this.#displayUrl(url)}
                            .iconSrc=${`page-icon:${url}`}
                            title=${url}
                            .href=${url}
                          ></smartwindow-chip>`
                      )}
                    </div>
                  </div>`
                : nothing}
              <div class="monitor-row">${this.#renderScheduleSummary()}</div>`}
        ${!this.editing ? this.#renderHistory() : nothing}
        ${!this.editing && checkLimit
          ? html`<p
              id="check-limit-reason"
              class="check-limit-reason"
              data-l10n-id=${monitorErrorL10nId(checkLimit.errorCode)}
            ></p>`
          : nothing}

        <div class="monitor-card-actions">
          ${!this.editing
            ? html`<moz-button
                  id="edit-button"
                  type="default"
                  @click=${this.#onEditToggle}
                  data-l10n-id="ai-tasks-alert-edit-button"
                  data-l10n-attrs="label"
                ></moz-button>
                <moz-button
                  id="pause-button"
                  type="default"
                  ?disabled=${agent.status?.kind === "paused" &&
                  !this.canResume}
                  @click=${() => {
                    const isPaused = agent.status?.kind === "paused";
                    this.#dispatch("agent-monitor-item:pause", {
                      id: agent.id,
                      paused: !isPaused,
                    });
                  }}
                  data-l10n-id=${agent.status?.kind === "paused"
                    ? "ai-tasks-alert-resume-button"
                    : "ai-tasks-alert-pause-button"}
                  data-l10n-attrs="label"
                ></moz-button>
                <moz-button
                  id="check-now-button"
                  type="default"
                  ?disabled=${!!checkLimit}
                  @click=${() =>
                    this.#dispatch("agent-monitor-item:check-now", {
                      id: agent.id,
                    })}
                  data-l10n-id="ai-tasks-alert-check-now-button"
                  data-l10n-attrs="label"
                ></moz-button>`
            : nothing}

          <span class="spacer"></span>
          ${this.editing
            ? html` <moz-button
                  id="cancel-edit-button"
                  type="secondary"
                  @click=${this.#onEditToggle}
                  data-l10n-id="ai-tasks-alert-cancel-button"
                  data-l10n-attrs="label"
                ></moz-button
                ><moz-button
                  type="primary"
                  @click=${this.#onSubmit}
                  data-l10n-id="ai-tasks-alert-save-button"
                  data-l10n-attrs="label"
                ></moz-button>`
            : html`
                <moz-button
                  class="delete-button"
                  type="icon"
                  iconsrc="chrome://global/skin/icons/delete.svg"
                  data-l10n-id="ai-tasks-alert-delete-button"
                  data-l10n-attrs="aria-label"
                  @click=${() =>
                    this.#dispatch("agent-monitor-item:delete", {
                      id: agent.id,
                    })}
                ></moz-button>
              `}
        </div>
      </div>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/agent-monitor-item.css"
      />
      ${this.mode === "create" ? this.#renderCreate() : this.#renderDisplay()}
    `;
  }
}

customElements.define("agent-monitor-item", AgentMonitorItem);
