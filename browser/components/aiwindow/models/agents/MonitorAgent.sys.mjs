/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  Monitor,
  MonitorLimitError,
  expiryRuleDays,
  monitorAgeMs,
  trimAndFilterWatchUrls,
  urlListsEqual,
  TOTAL_NUM_MONITORS,
  MONITOR_ERROR_CODES,
  MONITOR_EXPIRY_REASONS,
  MONITOR_PROMPT_VERSION,
  MONITOR_AGENTS_CHANGED_TOPIC,
  MONITOR_BLOCKED_STATE_CHANGED_TOPIC,
  MONITOR_CONDITION_MET_TOPIC,
  MONITOR_RUN_FAILED_TOPIC,
} from "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs";
import {
  Schedule,
  SCHEDULE_TYPES,
} from "moz-src:///browser/components/aiwindow/models/agents/Schedule.sys.mjs";

export {
  Monitor,
  TOTAL_NUM_MONITORS,
  MAX_HISTORY_ENTRIES,
  MONITOR_PROMPT_VERSION,
  TOTAL_NUM_URLS_IN_MONITOR,
  MONITOR_AGENTS_CHANGED_TOPIC,
  MONITOR_BLOCKED_STATE_CHANGED_TOPIC,
  MONITOR_CONDITION_MET_TOPIC,
  MONITOR_EXPIRY_REASONS,
  MONITOR_RUN_FAILED_TOPIC,
} from "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs";

// Notification body shown for each auto-expiry reason.
const EXPIRY_BODY_IDS = Object.freeze({
  [MONITOR_EXPIRY_REASONS.NO_MATCH]:
    "ai-tasks-monitor-expired-notification-body-no-match",
  [MONITOR_EXPIRY_REASONS.MAX_AGE]:
    "ai-tasks-monitor-expired-notification-body-max-age",
});

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  MonitorStore:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorStore.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    logModule: "MonitorAgent",
    maxLogLevelPref: "browser.smartwindow.monitorAgent.logLevel",
  })
);

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization(
      ["preview/aiWindow.ftl", "toolkit/branding/brandings.ftl"],
      true
    )
);

const AlertNotification = Components.Constructor(
  "@mozilla.org/alert-notification;1",
  "nsIAlertNotification",
  "initWithObject"
);

const TASKS_PAGE_URL = "about:smartwindowtasks";

let gMonitors = null;
let gLoadPromise = null;
let gShuttingDown = false;
let gSuspended = false;
const gNotifiedRunIds = new Set();
const gSnapshotRefreshPromises = new Map();

export const NOTIFICATION_ACTIONS = {
  SNOOZE: "monitor-snooze",
  DISMISS: "monitor-dismiss",
  RESUME: "monitor-resume",
};

const CREATE_SOURCES = new Set([
  "in_line_chat",
  "toolbar_panel",
  "about_page",
  "test",
]);

const AGENT_TYPE = "monitor";

// Which desktop notification was shown, recorded as the telemetry
// notification_type on the display and close events.
export const NOTIFICATION_TYPES = {
  CONDITION_MET: "condition_met",
  RUN_FAILED: "run_failed",
  CREATED: "created",
  EXPIRED: "expired",
};

const CHECK_BACKOFF_MS = 15 * 60 * 1000;

let gLastServiceAnswer = null;

const LAST_SERVICE_ANSWER_PREF = "browser.smartwindow.agent.lastServiceAnswer";

export const CHECK_LIMITS = Object.freeze({
  BUDGET: "budget",
  THROTTLE: "throttle",
});

const CHECK_LIMIT_POLICIES = Object.freeze({
  [CHECK_LIMITS.BUDGET]: {
    codes: new Set([MONITOR_ERROR_CODES.BUDGET_EXCEEDED]),
    // The agent's allowance is a seven-day budget on the service, which runs
    // to Monday 00:00 US/Eastern.
    // TODO: Bug 2091498 - the boundary is the service's to know, so read the
    // reset time it reports instead of repeating its schedule here.
    resumesAt: answeredAt => {
      const at =
        Temporal.Instant.fromEpochMilliseconds(answeredAt).toZonedDateTimeISO(
          "America/New_York"
        );
      // dayOfWeek is Monday 1 through Sunday 7.
      return at.startOfDay().add({ days: 8 - at.dayOfWeek }).epochMilliseconds;
    },
  },
  [CHECK_LIMITS.THROTTLE]: {
    codes: new Set([
      MONITOR_ERROR_CODES.RATE_LIMIT,
      MONITOR_ERROR_CODES.UPSTREAM_RATE_LIMIT,
      MONITOR_ERROR_CODES.FASTLY_RATE_LIMIT,
      // Shared service budgets have no known reset time,
      // so they are retried like a throttle
      MONITOR_ERROR_CODES.GLOBAL_BUDGET_EXCEEDED,
      MONITOR_ERROR_CODES.GUEST_BUDGET_EXCEEDED,
    ]),
    resumesAt: answeredAt => answeredAt + CHECK_BACKOFF_MS,
  },
});

function checkLimitPolicyFor(errorCode) {
  return Object.entries(CHECK_LIMIT_POLICIES).find(([, { codes }]) =>
    codes.has(errorCode)
  );
}

// Failures the user already knows about, or that only mean Firefox is going
// away: telling them their monitor could not run would be noise.
const UNREPORTED_ERROR_CODES = new Set([
  MONITOR_ERROR_CODES.CANCELED,
  MONITOR_ERROR_CODES.INTERRUPTED,
]);

/**
 * Treats history that already exists at load time as seen, so restoring
 * monitors on startup doesn't replay old alerts as fresh notifications.
 *
 * @param {Iterable<Monitor>} monitors
 */
function seedNotifiedRunIds(monitors) {
  for (const monitor of monitors) {
    for (const entry of monitor.history) {
      if (entry.conditionMet || entry.status === "error") {
        gNotifiedRunIds.add(entry.id);
      }
    }
  }
}

function isServiceAnswer(entry) {
  return entry?.status === "success" || !!checkLimitPolicyFor(entry?.errorCode);
}

/**
 * When a run finished, which is when the service answered it.
 *
 * @param {object} entry - A history entry, as run() leaves it
 * @returns {number} Epoch ms
 */
function serviceAnswerTime(entry) {
  return new Date(entry?.finishedAt).getTime();
}

/**
 * The last answer from the service, kept separately from the monitors. Their
 * history can't stand in for it, because deleting a task removes its history
 * along with any answer recorded there.
 *
 * @returns {?{answeredAt: number, errorCode: ?string}}
 */
function loadLastServiceAnswer() {
  try {
    const { answeredAt, errorCode } = JSON.parse(
      Services.prefs.getCharPref(LAST_SERVICE_ANSWER_PREF, "")
    );
    return Number.isFinite(answeredAt)
      ? { answeredAt, errorCode: errorCode ?? null }
      : null;
  } catch {
    return null;
  }
}

function saveLastServiceAnswer(answer) {
  Services.prefs.setCharPref(LAST_SERVICE_ANSWER_PREF, JSON.stringify(answer));
}

function isShuttingDown() {
  return (
    gShuttingDown ||
    Services.startup.isInOrBeyondShutdownPhase(
      Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
    )
  );
}

/**
 * Indicates that a MonitorAgent operation was interrupted by application
 * shutdown.
 */
class MonitorAgentShutdownError extends Error {
  constructor(options) {
    super("Monitor agent is shutting down.", options);
    this.name = "MonitorAgentShutdownError";
  }
}

function activeMonitorCount() {
  let count = 0;
  for (const monitor of gMonitors.values()) {
    if (monitor.enabled) {
      count++;
    }
  }
  return count;
}

function isClockField(value, max) {
  return Number.isInteger(value) && value >= 0 && value <= max;
}

// Works on both a Schedule instance and the raw schedule argument of a
// creation request; only validated values are recorded.
function buildScheduleTelemetryExtra(schedule) {
  if (!Object.values(SCHEDULE_TYPES).includes(schedule?.type)) {
    return {};
  }
  const extra = { schedule_type: schedule.type };
  if (schedule.type === SCHEDULE_TYPES.INTERVAL) {
    return extra;
  }
  if (isClockField(schedule.hour, 23) && isClockField(schedule.minute, 59)) {
    const pad = value => String(value).padStart(2, "0");
    extra.check_time = `${pad(schedule.hour)}:${pad(schedule.minute)}`;
  }
  if (
    schedule.type === SCHEDULE_TYPES.WEEKLY &&
    isClockField(schedule.weekday, 6)
  ) {
    extra.check_weekday = schedule.weekday;
  }
  return extra;
}

function monitorTelemetryExtra(monitor) {
  return {
    monitors: gMonitors?.size ?? 0,
    urls: monitor.watchUrls.length,
    length: monitor.monitorPrompt.length,
    age: monitorAgeMs(monitor),
    active_age: monitorAgeMs(monitor, monitor.activeSince),
    ...buildScheduleTelemetryExtra(monitor.schedule),
    prompt_version: MONITOR_PROMPT_VERSION,
    enabled: monitor.enabled,
    action_id: monitor.id,
    agent: AGENT_TYPE,
  };
}

// Optional context the front end passes with an action, validated before it
// is recorded. Keys the caller did not provide stay unset.
function buildTelemetryContextExtra({ source, chatId, messageSeq } = {}) {
  const extra = {};
  if (source !== undefined) {
    extra.source = CREATE_SOURCES.has(source) ? source : "unknown";
  }
  if (typeof chatId === "string" && chatId.length <= 64) {
    extra.chat_id = chatId;
  }
  if (Number.isInteger(messageSeq) && messageSeq >= 0) {
    extra.message_seq = messageSeq;
  }
  return extra;
}

function buildCreationArgsExtra({ prompt, watchUrls, schedule }) {
  return {
    agent: AGENT_TYPE,
    urls: Array.isArray(watchUrls) ? watchUrls.length : 0,
    length: String(prompt ?? "").length,
    ...buildScheduleTelemetryExtra(schedule),
  };
}

function getCreationErrorCode(error) {
  if (error instanceof MonitorAgentShutdownError) {
    return MONITOR_ERROR_CODES.INTERRUPTED;
  }
  if (DOMException.isInstance(error)) {
    return "storage_error";
  }
  if (error instanceof MonitorLimitError) {
    return "limit_reached";
  }
  const message = error?.message ?? "";
  if (/invalid|cannot watch more than/i.test(message)) {
    return "invalid_input";
  }
  return MONITOR_ERROR_CODES.UNKNOWN;
}

// executionSeq is captured when the notification is shown so a later click
// still joins to the run the notification was about.
function buildNotificationTelemetryExtra(
  monitor,
  notificationType,
  executionSeq
) {
  const extra = {
    ...monitorTelemetryExtra(monitor),
    notification_type: notificationType,
    execution_seq: executionSeq,
  };
  if (notificationType === NOTIFICATION_TYPES.CONDITION_MET) {
    extra.outcome = true;
  }
  return extra;
}

// The user paused the monitor; auto-expiry pauses record MONITOR_EXPIRY_REASONS.
const PAUSE_REASON_USER = "user";

// A plain pause or resume is not an edit; an edit that also flips the enabled
// state records both.
function recordUpdateTelemetry(
  monitor,
  { updates, wasEnabled, telemetryContext }
) {
  const extra = {
    ...monitorTelemetryExtra(monitor),
    ...buildTelemetryContextExtra(telemetryContext),
  };
  if (!Object.keys(updates).every(key => key === "enabled")) {
    Glean.smartWindow.agenticActionEditComplete.record(extra);
  }
  if (wasEnabled === monitor.enabled) {
    return;
  }
  if (monitor.enabled) {
    Glean.smartWindow.agenticActionResume.record(extra);
  } else {
    Glean.smartWindow.agenticActionPause.record({
      ...extra,
      reason: PAUSE_REASON_USER,
    });
  }
}

/**
 * Indicates that a MonitorAgent operation was refused because Smart Window is
 * blocked by AI Controls.
 */
class MonitorAgentBlockedError extends Error {
  constructor() {
    super("Smart Window is blocked by AI Controls.");
    this.name = "MonitorAgentBlockedError";
  }
}

/**
 * Indicates that a check was refused because the service is still holding
 * checks back. Carries the limit so a caller can say when they resume.
 */
class MonitorAgentLimitedError extends Error {
  constructor(checkLimit) {
    super(`Checks are unavailable until ${new Date(checkLimit.until)}.`);
    this.name = "MonitorAgentLimitedError";
    this.checkLimit = checkLimit;
  }
}

/**
 * Monitor Agent state and behavior.
 *
 * Monitor agent is responsible for managing the collection of monitors, including creating,
 * updating, deleting, and running them on a schedule.
 * It also handles persisting the monitor state to IndexedDB and notifying observers of changes.
 */
export const MonitorAgent = {
  get isBlocked() {
    return lazy.AIWindow.isBlocked;
  },

  async init() {
    if (this.isBlocked) {
      gSuspended = true;
      return;
    }
    gSuspended = false;
    try {
      await this._ensureLoaded();
    } catch (error) {
      if (error instanceof MonitorAgentShutdownError) {
        return;
      }
      throw error;
    }
    if (isShuttingDown()) {
      return;
    }

    for (const monitor of gMonitors.values()) {
      // Smart Window may have been blocked while loading or expiring monitors
      if (this.isBlocked) {
        gSuspended = true;
        return;
      }
      monitor.restore();
      // a monitor that outlived its expiry window while the browser was
      // closed is paused right away instead of waiting for its next run
      const expiryReason = monitor.enabled && monitor.getExpiryReason();
      if (expiryReason) {
        try {
          await this._expireMonitor(monitor, expiryReason);
        } catch (error) {
          lazy.log.error(`Failed to expire monitor ${monitor.id}`, error);
        }
        continue;
      }
      monitor.scheduleNextRun();
    }
  },

  uninit() {
    gShuttingDown = true;
    if (!gMonitors) {
      return;
    }

    for (const monitor of gMonitors.values()) {
      monitor.dispose();
    }
  },

  /**
   * Suspends active monitors when Smart Window is blocked by AI Controls,
   * and resumes monitoring when the block is lifted.
   */
  async onBlockedStateChanged() {
    const unblocked = !this.isBlocked;

    if (unblocked) {
      if (gSuspended) {
        Services.obs.notifyObservers(null, MONITOR_BLOCKED_STATE_CHANGED_TOPIC);
        // Resume monitoring since Smart Window is no longer blocked
        await this.init();
      }
      return;
    }

    // If already suspended, no need to do anything further
    if (gSuspended) {
      return;
    }

    // State transition: mark as suspended since Smart Window is blocked
    gSuspended = true;

    for (const monitor of gMonitors?.values() ?? []) {
      monitor.dispose(MONITOR_ERROR_CODES.CANCELED);
    }
    Services.obs.notifyObservers(null, MONITOR_BLOCKED_STATE_CHANGED_TOPIC);
  },

  async listMonitors() {
    await this._ensureLoaded();
    const checkLimit = this.checkLimit();
    return Array.from(gMonitors.values(), monitor => ({
      ...monitor.toSerializable(),
      checkLimit,
    }));
  },

  /**
   * Records a finished run if the service answered it, so the most recent
   * answer decides whether checks are limited.
   *
   * @param {object} entry - The run's history entry
   */
  _noteServiceAnswer(entry) {
    if (isServiceAnswer(entry)) {
      gLastServiceAnswer = {
        answeredAt: serviceAnswerTime(entry),
        errorCode: entry.errorCode ?? null,
      };
      saveLastServiceAnswer(gLastServiceAnswer);
    }
  },

  /**
   * Returns when Check now can be used again after the service refused a check.
   *
   * @returns {?{until: number, reason: string, errorCode: string}}
   */
  checkLimit() {
    const { answeredAt, errorCode } = gLastServiceAnswer ?? {};
    const [reason, policy] = checkLimitPolicyFor(errorCode) ?? [];
    const until = policy?.resumesAt(answeredAt);
    return until > Date.now() ? { until, reason, errorCode } : null;
  },

  /**
   * Creates a new monitor to watch specified URLs for condition changes.
   *
   * @param {object} options - Configuration for the new monitor
   * @param {string} options.prompt - The condition to monitor for
   * @param {string[]} options.watchUrls - Array of URLs to watch
   * @param {string} [options.pageTitle=""] - Optional title for the monitor
   * @param {object} options.schedule - Schedule configuration (type, hours, etc.)
   * @param {string} [options.source="unknown"] - Surface the monitor is created from, for telemetry: "in_line_chat", "toolbar_panel", "about_page" or "test"
   * @param {string} [options.chatId] - Telemetry context the front end passes for chat-originated creations: the chat's id
   * @param {number} [options.messageSeq] - Telemetry context the front end passes for chat-originated creations: the message number within that chat
   * @returns {Promise<string>} The ID of the created monitor
   * @throws {MonitorLimitError} If the maximum number of active monitors has been reached
   */
  async createMonitor({
    source = "unknown",
    chatId = null,
    messageSeq = null,
    ...args
  }) {
    if (this.isBlocked) {
      throw new MonitorAgentBlockedError();
    }
    const context = buildTelemetryContextExtra({ source, chatId, messageSeq });
    // Load the store first so the submit event counts the stored monitors;
    // a load failure is still reported through create_complete below.
    const loadError = await this._ensureLoaded().then(
      () => null,
      error => error
    );
    const submitExtra = {
      ...context,
      ...buildCreationArgsExtra(args),
      monitors: gMonitors?.size ?? 0,
    };
    Glean.smartWindow.agenticActionCreateSubmit.record(submitExtra);
    try {
      if (loadError) {
        throw loadError;
      }
      const id = await this._createMonitor(args);
      const monitor = gMonitors.get(id);
      Glean.smartWindow.agenticActionCreateComplete.record({
        ...monitorTelemetryExtra(monitor),
        ...context,
        success: true,
      });
      this._notifyMonitorCreated(monitor);
      return id;
    } catch (error) {
      Glean.smartWindow.agenticActionCreateComplete.record({
        ...submitExtra,
        success: false,
        error_code: getCreationErrorCode(error),
      });
      throw error;
    }
  },

  async _createMonitor({ prompt, watchUrls, pageTitle = "", schedule }) {
    await this._ensureLoaded();
    if (activeMonitorCount() >= TOTAL_NUM_MONITORS) {
      throw new MonitorLimitError(TOTAL_NUM_MONITORS);
    }

    const monitor = new Monitor({
      title: pageTitle,
      monitorPrompt: prompt,
      watchUrls: trimAndFilterWatchUrls(watchUrls),
      schedule: Schedule.fromJSON(schedule),
    });
    try {
      gMonitors.set(monitor.id, monitor);
      await this._saveAndNotify(monitor);
    } catch (error) {
      gMonitors.delete(monitor.id);
      throw error;
    }
    monitor.scheduleNextRun();
    this._refreshInitialSnapshot(monitor);
    return monitor.id;
  },

  /**
   * Updates a monitor's definition or enabled state.
   *
   * @param {string} id - The monitor ID
   * @param {object} updates - Fields to change: title, monitorPrompt,
   *   watchUrls, schedule and/or enabled
   * @param {object} [telemetryContext] - Optional source, chatId and
   *   messageSeq the front end passes for telemetry
   * @returns {Promise<void>}
   */
  async updateMonitor(id, updates, telemetryContext = {}) {
    if (this.isBlocked) {
      throw new MonitorAgentBlockedError();
    }
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      return;
    }

    const next = {
      activeSince: monitor.activeSince,
      enabled: monitor.enabled,
      expiry: monitor.expiry,
      monitorPrompt: monitor.monitorPrompt,
      nextRunTime: monitor.nextRunTime,
      schedule: monitor.schedule,
      title: monitor.title,
      watchUrls: monitor.watchUrls.slice(),
    };

    // clean up and validate updates
    if ("monitorPrompt" in updates) {
      next.monitorPrompt = String(updates.monitorPrompt ?? "").trim();
    }
    if ("watchUrls" in updates) {
      next.watchUrls = trimAndFilterWatchUrls(updates.watchUrls);
    }
    if ("title" in updates) {
      next.title = String(updates.title ?? "").trim();
    }
    if ("enabled" in updates) {
      next.enabled = !!updates.enabled;
    }
    if ("schedule" in updates) {
      next.schedule = Schedule.fromJSON(updates.schedule);
      // Base the next run on now rather than a possibly-stale lastRunTime so a
      // schedule edit can't resolve to a time in the past and fire immediately.
      next.nextRunTime = next.schedule
        .getNextRunTime(new Date().toISOString())
        .toISOString();
    } else if (!monitor.enabled && next.enabled) {
      if (activeMonitorCount() >= TOTAL_NUM_MONITORS) {
        throw new MonitorLimitError(TOTAL_NUM_MONITORS);
      }
      // else if so we don't compute nextRunTime twice
      // Re-enabling: schedule the next run a full interval from now rather than
      // reusing a stale nextRunTime that may already be in the past.
      next.nextRunTime = next.schedule
        .getNextRunTime(new Date().toISOString())
        .toISOString();
    }

    if (!next.monitorPrompt || !next.watchUrls.length) {
      throw new Error("Monitor is invalid.");
    }

    // The stored snapshot is the baseline from when the user last committed
    // the monitor's definition, so any definition edit (title, prompt,
    // schedule, or watch URLs) replaces it with one captured at edit time.
    // Pause/resume toggles are not edits and keep the baseline.
    const scheduleChanged =
      "schedule" in updates &&
      JSON.stringify({ ...next.schedule }) !==
        JSON.stringify({ ...monitor.schedule });
    const definitionChanged =
      scheduleChanged ||
      next.monitorPrompt !== monitor.monitorPrompt ||
      next.title !== monitor.title ||
      !urlListsEqual(next.watchUrls, monitor.watchUrls);

    const now = new Date().toISOString();
    const resumed = !monitor.enabled && next.enabled;
    // Only a resume clears the expiry record; an edit of a still-paused
    // monitor keeps the reason it paused itself.
    if (resumed) {
      next.expiry = null;
    }
    // Resuming or editing restarts the auto-expiry windows, otherwise a
    // monitor resumed after expiring would pause itself again on its next run.
    // lastMatchAt is kept: the no-match window starts at the later of it and
    // activeSince, so an older match no longer counts anyway.
    if (resumed || definitionChanged) {
      next.activeSince = now;
    }
    next.updatedAt = now;
    if (definitionChanged) {
      next.initialSnapshot = null;
    }

    // save old in case the update fails, so we can restore it; every field
    // written to the monitor is a key of next, so previous is derived from it
    const previous = Object.fromEntries(
      Object.keys(next).map(key => [key, monitor[key]])
    );

    Object.assign(monitor, next);
    if (definitionChanged) {
      // stop any in-flight capture so a stale baseline can't land post-edit
      monitor.cancelSnapshotCapture();
    }
    try {
      await this._saveAndNotify(monitor);
    } catch (error) {
      Object.assign(monitor, previous);
      throw error;
    }
    monitor.scheduleNextRun();
    if (definitionChanged) {
      this._refreshInitialSnapshot(monitor);
    }
    recordUpdateTelemetry(monitor, {
      updates,
      wasEnabled: previous.enabled,
      telemetryContext,
    });
  },

  /**
   * Pauses or unpauses a monitor by toggling its enabled state.
   *
   * @param {string} id - The monitor ID
   * @param {boolean} [pause] - Optional. If provided, sets enabled to !pause.
   *                            If not provided, toggles the current enabled state.
   * @param {object} [telemetryContext] - Optional source, chatId and
   *   messageSeq the front end passes for telemetry
   * @returns {Promise<void>}
   */
  async pauseMonitor(id, pause, telemetryContext = {}) {
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      throw new Error(`Monitor with id ${id} not found`);
    }

    // Determine the new enabled state
    let newEnabledState;
    if (pause === undefined) {
      // Toggle current state
      newEnabledState = !monitor.enabled;
    } else {
      // Set to opposite of pause (pause=true means enabled=false)
      newEnabledState = !pause;
    }

    // Use updateMonitor to handle the state change
    await this.updateMonitor(
      id,
      { enabled: newEnabledState },
      telemetryContext
    );
  },

  /**
   * @param {string} id - The monitor ID
   * @param {object} [telemetryContext] - Optional source, chatId and
   *   messageSeq the front end passes for telemetry
   * @returns {Promise<boolean>} Whether a monitor with that id was deleted
   */
  async deleteMonitor(id, telemetryContext = {}) {
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      return false;
    }

    try {
      monitor.dispose(MONITOR_ERROR_CODES.CANCELED);
      gMonitors.delete(id);
      await lazy.MonitorStore.deleteMonitor(id);
      gSnapshotRefreshPromises.delete(id);
    } catch (error) {
      monitor.restore();
      gMonitors.set(id, monitor);
      monitor.scheduleNextRun();
      throw error;
    } finally {
      Services.obs.notifyObservers(null, MONITOR_AGENTS_CHANGED_TOPIC);
    }
    this._updateActionGauges();
    Glean.smartWindow.agenticActionDelete.record({
      ...monitorTelemetryExtra(monitor),
      ...buildTelemetryContextExtra(telemetryContext),
    });
    return true;
  },

  async runNow(id) {
    if (this.isBlocked) {
      throw new MonitorAgentBlockedError();
    }
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      return;
    }

    // The card disables Check now while a limit is in force, so reaching here
    // means a limit landed between the render and the click.
    const checkLimit = this.checkLimit();
    if (checkLimit) {
      throw new MonitorAgentLimitedError(checkLimit);
    }
    await monitor.run({ manual: true });
  },

  async _ensureLoaded() {
    if (isShuttingDown()) {
      throw new MonitorAgentShutdownError();
    }
    if (gMonitors) {
      return;
    }
    if (gLoadPromise) {
      await gLoadPromise;
      return;
    }

    gLoadPromise = this._loadMonitors().catch(error => {
      if (isShuttingDown() && !(error instanceof MonitorAgentShutdownError)) {
        throw new MonitorAgentShutdownError({ cause: error });
      }
      throw error;
    });
    try {
      await gLoadPromise;
    } finally {
      gLoadPromise = null;
    }
  },

  async _loadMonitors() {
    const monitors = new Map();
    for (const savedMonitor of await lazy.MonitorStore.listMonitors()) {
      try {
        const monitor = Monitor.fromJSON(savedMonitor);
        monitors.set(monitor.id, monitor);
      } catch (error) {
        lazy.log.warn(`Skipping invalid stored monitor: ${error.message}`);
      }
    }

    if (isShuttingDown()) {
      throw new MonitorAgentShutdownError();
    }

    gMonitors = monitors;
    gLastServiceAnswer = loadLastServiceAnswer();

    seedNotifiedRunIds(monitors.values());
    this._updateActionGauges();
  },

  /**
   * Captures the monitor's initial snapshot in the background and persists it
   * once done. Never blocks creation or editing; failures are logged and the
   * monitor keeps working without a snapshot.
   *
   * @param {Monitor} monitor
   */
  _refreshInitialSnapshot(monitor) {
    const refreshPromise = monitor
      .ensureInitialSnapshot()
      .then(async snapshot => {
        // the monitor may have been deleted or replaced while capturing
        if (gMonitors?.get(monitor.id) === monitor) {
          await this._saveAndNotify(monitor);
        }
        return snapshot;
      });
    gSnapshotRefreshPromises.set(monitor.id, refreshPromise);
    refreshPromise
      .catch(error => {
        lazy.log.warn(
          `Failed to capture initial snapshot for monitor ${monitor.id}: ${
            error.message ?? error
          }`
        );
      })
      .finally(() => {
        if (gSnapshotRefreshPromises.get(monitor.id) === refreshPromise) {
          gSnapshotRefreshPromises.delete(monitor.id);
        }
      });
  },

  async _saveAndNotify(monitor = null) {
    await this._ensureLoaded();
    // persisting a single known monitor avoids rewriting every monitor
    if (monitor && gMonitors.has(monitor.id)) {
      await lazy.MonitorStore.saveMonitor(monitor);
      // bulk path is only needed for migration and unscoped saves
    } else {
      await lazy.MonitorStore.saveMonitors(Array.from(gMonitors.values()));
    }
    this._updateActionGauges();
    Services.obs.notifyObservers(null, MONITOR_AGENTS_CHANGED_TOPIC);

    if (monitor) {
      this._notifyIfConditionMet(monitor);
      this._notifyIfRunFailed(monitor);
    }
  },

  _updateActionGauges() {
    if (!gMonitors) {
      return;
    }
    const active = activeMonitorCount();
    Glean.smartWindow.agentActiveActions[AGENT_TYPE].set(active);
    Glean.smartWindow.agentPausedActions[AGENT_TYPE].set(
      gMonitors.size - active
    );
  },

  _telemetryExtra(monitor) {
    return monitorTelemetryExtra(monitor);
  },

  /**
   * Pauses a monitor that hit an auto-expiry rule, records why so the UI can
   * tell, and lets the user know with a desktop notification offering to
   * resume it.
   *
   * @param {Monitor} monitor
   * @param {string} reason - One of MONITOR_EXPIRY_REASONS.
   */
  async _expireMonitor(monitor, reason) {
    const now = new Date().toISOString();
    const previous = {
      enabled: monitor.enabled,
      expiry: monitor.expiry,
      updatedAt: monitor.updatedAt,
    };
    monitor.clearTimer();
    monitor.enabled = false;
    monitor.expiry = { expiredAt: now, reason };
    monitor.updatedAt = now;
    try {
      await this._saveAndNotify(monitor);
    } catch (error) {
      // keep running in memory to match the store, and try again on the next
      // scheduled slot rather than right away
      Object.assign(monitor, previous);
      monitor.nextRunTime = monitor.schedule.getNextRunTime(now).toISOString();
      monitor.scheduleNextRun();
      throw error;
    }
    lazy.log.info(`Monitor ${monitor.id} expired: ${reason}`);
    Glean.smartWindow.agenticActionPause.record({
      ...monitorTelemetryExtra(monitor),
      reason,
    });
    this._notifyExpired(monitor, reason);
  },

  /**
   * Desktop notification telling the user a monitor paused itself. Clicking
   * the body opens the tasks page, the "resume" action turns the monitor back
   * on. Sent even when the monitor's match notifications are muted, since it
   * is about the monitor stopping rather than a match.
   *
   * @param {Monitor} monitor
   * @param {string} reason - One of MONITOR_EXPIRY_REASONS.
   */
  _notifyExpired(monitor, reason) {
    const bodyId = EXPIRY_BODY_IDS[reason];
    if (!bodyId) {
      lazy.log.error(`Unknown monitor expiry reason: ${reason}`);
      return;
    }
    const id = monitor.id;
    const executionSeq = monitor.runCount;
    const recordClick = clickReason =>
      Glean.smartWindow.agenticActionNotificationClose.record({
        ...buildNotificationTelemetryExtra(
          monitor,
          NOTIFICATION_TYPES.EXPIRED,
          executionSeq
        ),
        reason: clickReason,
      });

    const shown = this._showMonitorAlert(monitor, {
      textId: bodyId,
      textArgs: { days: expiryRuleDays(reason) },
      actions: [
        {
          action: NOTIFICATION_ACTIONS.RESUME,
          titleId: "ai-tasks-monitor-expired-notification-resume",
        },
      ],
      onClick: action => {
        if (action === NOTIFICATION_ACTIONS.RESUME) {
          recordClick("resume");
          this.pauseMonitor(id, false).catch(error =>
            lazy.log.error("Failed to resume expired monitor", error)
          );
          return;
        }
        if (!action) {
          recordClick("open_tasks");
          this._openWatchedUrl(TASKS_PAGE_URL);
        }
      },
    });
    if (shown) {
      Glean.smartWindow.agenticActionNotificationDisplay.record(
        buildNotificationTelemetryExtra(
          monitor,
          NOTIFICATION_TYPES.EXPIRED,
          executionSeq
        )
      );
    }
  },

  /**
   * Shows a desktop notification every time a monitor run meets its condition.
   * The notification carries two actions:
   *  - "snooze": hold off checks until the next day.
   *  - "dismiss": stop notifying while the monitor keeps running.
   * Clicking the body opens the watched page.
   *
   * Muting only silences the desktop notification: the condition-met topic is
   * still fired so the passive dot on the toolbar button stays accurate.
   *
   * @param {Monitor} monitor - The monitor whose latest run just saved
   */
  _notifyIfConditionMet(monitor) {
    const entry = monitor.history.at(-1);
    if (!entry || entry.status !== "success" || !entry.conditionMet) {
      return;
    }

    if (gNotifiedRunIds.has(entry.id)) {
      return;
    }
    gNotifiedRunIds.add(entry.id);

    Services.obs.notifyObservers(null, MONITOR_CONDITION_MET_TOPIC, monitor.id);

    if (monitor.notificationsMuted) {
      return;
    }

    const executionSeq = monitor.runCount;
    const shown = this._showMonitorAlert(monitor, {
      text: entry.resultExplanation,
      textId: "ai-tasks-monitor-notification-body",
      textArgs: { pageCount: monitor.watchUrls.length },
      ...this._runNotificationActions(
        monitor,
        NOTIFICATION_TYPES.CONDITION_MET,
        executionSeq
      ),
    });

    if (shown) {
      Glean.smartWindow.agenticActionNotificationDisplay.record(
        buildNotificationTelemetryExtra(
          monitor,
          NOTIFICATION_TYPES.CONDITION_MET,
          executionSeq
        )
      );
    }
  },

  /**
   * Tells the user that a monitor could not check, so a monitor that keeps
   * failing does not fail silently. Follows the condition-met path: the same
   * topic-then-notification shape and the same actions, and it stays quiet
   * about a run it has already reported.
   *
   * A run the user cancelled or that shutdown cut short is not news, so it is
   * left alone entirely rather than reported through either channel.
   *
   * @param {Monitor} monitor - The monitor whose latest run just saved
   */
  _notifyIfRunFailed(monitor) {
    const entry = monitor.history.at(-1);
    if (
      !entry ||
      entry.status !== "error" ||
      UNREPORTED_ERROR_CODES.has(entry.errorCode)
    ) {
      return;
    }

    if (gNotifiedRunIds.has(entry.id)) {
      return;
    }
    gNotifiedRunIds.add(entry.id);

    Services.obs.notifyObservers(null, MONITOR_RUN_FAILED_TOPIC, monitor.id);

    if (monitor.notificationsMuted) {
      return;
    }

    // resultExplanation holds the raw error message, so the body is the
    // localized copy rather than anything the run produced.
    const executionSeq = monitor.runCount;
    const shown = this._showMonitorAlert(monitor, {
      textId: "ai-tasks-monitor-error-notification-body",
      ...this._runNotificationActions(
        monitor,
        NOTIFICATION_TYPES.RUN_FAILED,
        executionSeq
      ),
    });

    if (shown) {
      Glean.smartWindow.agenticActionNotificationDisplay.record(
        buildNotificationTelemetryExtra(
          monitor,
          NOTIFICATION_TYPES.RUN_FAILED,
          executionSeq
        )
      );
    }
  },

  /**
   * The snooze and dismiss actions that every notification about a run
   * carries, with the click handling for them and for the body, which opens
   * the watched page. Spread into a _showMonitorAlert call.
   *
   * @param {Monitor} monitor - The monitor the notification is about
   * @param {string} notificationType - A NOTIFICATION_TYPES entry, recorded
   *   with each click so a match and a failed check can be told apart.
   * @param {number} executionSeq - The monitor's run count when the
   *   notification was shown, repeated on each click so it joins to that run.
   * @returns {{actions: object[], onClick: Function}}
   */
  _runNotificationActions(monitor, notificationType, executionSeq) {
    const url = monitor.watchUrls[0];
    const id = monitor.id;
    const recordClick = reason =>
      Glean.smartWindow.agenticActionNotificationClose.record({
        ...buildNotificationTelemetryExtra(
          monitor,
          notificationType,
          executionSeq
        ),
        reason,
      });

    return {
      actions: [
        {
          action: NOTIFICATION_ACTIONS.SNOOZE,
          titleId: "ai-tasks-monitor-notification-snooze",
        },
        {
          action: NOTIFICATION_ACTIONS.DISMISS,
          titleId: "ai-tasks-monitor-notification-dismiss",
        },
      ],
      onClick: action => {
        if (!action) {
          if (url) {
            recordClick("open_url");
            this._openWatchedUrl(url);
          }
          return;
        }
        if (action === NOTIFICATION_ACTIONS.SNOOZE) {
          recordClick("snooze");
          this.snoozeMonitor(id).catch(error =>
            lazy.log.error("Failed to snooze monitor", error)
          );
          return;
        }
        if (action === NOTIFICATION_ACTIONS.DISMISS) {
          recordClick("dismiss");
          this.muteMonitorNotifications(id).catch(error =>
            lazy.log.error("Failed to mute monitor notifications", error)
          );
        }
      },
    };
  },

  /**
   * Shows a one-off desktop notification right after a monitor is created so
   * the user knows a match will arrive the same way. Clicking the body opens
   * the tasks page listing the monitors.
   *
   * @param {Monitor} monitor - The monitor that was just created
   */
  _notifyMonitorCreated(monitor) {
    const executionSeq = monitor.runCount;
    const shown = this._showMonitorAlert(monitor, {
      textId: "ai-tasks-monitor-created-notification-body",
      textArgs: {
        site: URL.parse(monitor.watchUrls[0])?.hostname ?? "",
        extraCount: monitor.watchUrls.length - 1,
      },
      onClick: action => {
        if (!action) {
          Glean.smartWindow.agenticActionNotificationClose.record({
            ...buildNotificationTelemetryExtra(
              monitor,
              NOTIFICATION_TYPES.CREATED,
              executionSeq
            ),
            reason: "open_tasks",
          });
          this._openWatchedUrl(TASKS_PAGE_URL);
        }
      },
    });
    if (shown) {
      Glean.smartWindow.agenticActionNotificationDisplay.record(
        buildNotificationTelemetryExtra(
          monitor,
          NOTIFICATION_TYPES.CREATED,
          executionSeq
        )
      );
    }
  },

  /**
   * Shows a desktop notification about a monitor, titled with the monitor's
   * name. Best effort: any failure is logged and never reaches the caller, so
   * a notification problem cannot fail the operation that triggered it.
   *
   * @param {Monitor} monitor - The monitor the notification is about
   * @param {object} options
   * @param {string} [options.text] - Body text; when empty, textId is used
   * @param {string} options.textId - Fluent id of the body text
   * @param {object} [options.textArgs] - Fluent arguments for textId
   * @param {Array<{action: string, titleId: string}>} [options.actions] -
   *   Action buttons, each with the Fluent id of its label
   * @param {(action: string|null) => void} options.onClick - Called with the
   *   clicked action, or null when the body itself was clicked
   * @returns {boolean} Whether the notification was shown
   */
  _showMonitorAlert(
    monitor,
    { text, textId, textArgs, actions = [], onClick }
  ) {
    try {
      const [titleFallback, body, ...actionTitles] = lazy.l10n.formatValuesSync(
        [
          "ai-tasks-monitor-notification-title",
          { id: textId, args: textArgs },
          ...actions.map(({ titleId }) => titleId),
        ]
      );
      const alertsService = Cc["@mozilla.org/alerts-service;1"].getService(
        Ci.nsIAlertsService
      );
      const observer = {
        observe: (subject, topic) => {
          if (topic !== "alertclickcallback") {
            return;
          }
          onClick(
            subject ? subject.QueryInterface(Ci.nsIAlertAction).action : null
          );
        },
      };
      const alert = new AlertNotification({
        title: monitor.title || titleFallback,
        text: text || body,
        textClickable: true,
        actions: actions.map(({ action }, i) => ({
          action,
          title: actionTitles[i],
        })),
      });
      alertsService.showAlert(alert, observer);
      return true;
    } catch (error) {
      lazy.log.error("Failed to show monitor notification", error);
      return false;
    }
  },

  /**
   * Opens a watched url from a notification click in a browser window.
   * If the only window open is private or none is open at all
   * a fresh window is open with the watched url.
   *
   * @param {string} url - The URL to open
   */
  _openWatchedUrl(url) {
    const win = lazy.BrowserWindowTracker.getTopWindow({ private: false });
    if (win) {
      win.openTrustedLinkIn(url, "tab");
      return;
    }
    const args = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    args.data = url;
    lazy.BrowserWindowTracker.openWindow({ args });
  },

  /**
   * Snoozes a monitor for roughly a day. Blacks out the next 24h and then
   * resumes at the next time that matches the monitor's schedule
   *
   * @param {string} id - The monitor id
   */
  async snoozeMonitor(id) {
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      return;
    }
    const ONE_DAY_MS = 24 * 60 * 60 * 1000;
    const blackoutEnd = Date.now() + ONE_DAY_MS;
    let next = monitor.schedule.getNextRunTime(monitor.lastRunTime);
    for (let i = 0; next.getTime() < blackoutEnd && i < 400; i++) {
      next = monitor.schedule.getNextRunTime(next.toISOString());
    }
    monitor.nextRunTime = next.toISOString();
    monitor.scheduleNextRun();
    await this._saveAndNotify(monitor);
  },

  /**
   * Stops desktop notifications for a monitor
   *
   * @param {string} id - The monitor id
   */
  async muteMonitorNotifications(id) {
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      return;
    }
    monitor.notificationsMuted = true;
    await this._saveAndNotify(monitor);
  },

  _unloadForTesting() {
    this.uninit();
    gMonitors = null;
    gLastServiceAnswer = null;
    gLoadPromise = null;
    gNotifiedRunIds.clear();
    gSnapshotRefreshPromises.clear();
    gShuttingDown = false;
    gSuspended = false;
  },

  /**
   * Waits for the latest background snapshot capture and its persistence.
   *
   * @param {string} id
   * @returns {Promise<{ capturedAt: string, pageContent: string }>}
   */
  async _waitForSnapshotForTesting(id) {
    await this._ensureLoaded();
    const monitor = gMonitors.get(id);
    if (!monitor) {
      throw new Error(`Monitor with id ${id} not found`);
    }

    const refreshPromise = gSnapshotRefreshPromises.get(id);
    if (refreshPromise) {
      return refreshPromise;
    }
    if (monitor.initialSnapshot) {
      return monitor.initialSnapshot;
    }
    throw new Error(`Monitor with id ${id} has no pending snapshot capture`);
  },

  async _resetForTesting() {
    this._unloadForTesting();
    Services.prefs.clearUserPref(LAST_SERVICE_ANSWER_PREF);
    await lazy.MonitorStore.destroyDatabase();
  },
};
