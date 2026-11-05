/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  FEATURE_MAJOR_VERSIONS,
  MODEL_FEATURES,
  renderPrompt,
  makeJSONSchemaBlob,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";
import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { Schedule } from "moz-src:///browser/components/aiwindow/models/agents/Schedule.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  GetPageContent: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  MonitorAgent:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs",
  ScheduledTask: "resource://gre/modules/ScheduledTask.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    logModule: "MonitorAgent",
    maxLogLevelPref: "browser.smartwindow.monitorAgent.logLevel",
  })
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "expiryNoMatchDays",
  "browser.smartwindow.agent.expiry.noMatchDays"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "expiryMaxAgeDays",
  "browser.smartwindow.agent.expiry.maxAgeDays"
);

// Milliseconds in a day, to turn the expiry prefs (in days) into elapsed time.
const DAY_MS = 24 * 60 * 60 * 1000;

// TODO: Move these constants to RS: https://bugzilla.mozilla.org/show_bug.cgi?id=2054153
export const MAX_HISTORY_ENTRIES = 30;
const MONITOR_RUN_TIMEOUT_MS = 5 * 60 * 1000;
// How long monitor checks wait after load for client-rendered content.
export const PAGE_SETTLE_DELAY_MS = 10 * 1000;
let pageSettleDelayMs = PAGE_SETTLE_DELAY_MS;

/**
 * @param {number} delayMs
 */
export function setPageSettleDelayForTesting(delayMs) {
  pageSettleDelayMs = delayMs;
}
// Cap on enabled monitors; paused monitors are not counted.
export const TOTAL_NUM_MONITORS = 15;
export const TOTAL_NUM_URLS_IN_MONITOR = 5;
export const MONITOR_PROMPT_VERSION = String(
  FEATURE_MAJOR_VERSIONS[MODEL_FEATURES.AGENT_MONITOR]
);

// For observers, the topic is "smartwindow-monitor-agents-changed" and the subject is an array of all monitors in their serializable form.
export const MONITOR_AGENTS_CHANGED_TOPIC =
  "smartwindow-monitor-agents-changed";

// Notified when monitors are suspended or resumed because Smart Window was
// blocked or unblocked in AI Controls.
export const MONITOR_BLOCKED_STATE_CHANGED_TOPIC =
  "smartwindow-monitor-blocked-state-changed";

// Failure categories for monitor runs. Stored on error history entries as
// errorCode for the UI and reused as the telemetry error_code. ACTIVE_LIMIT is
// the one non-run code: creating or resuming a monitor past the active cap.
export const MONITOR_ERROR_CODES = Object.freeze({
  NETWORK: "network_error",
  TIMEOUT: "timeout",
  BUDGET_EXCEEDED: "budget_exceeded",
  RATE_LIMIT: "rate_limit",
  REQUEST_TOO_LARGE: "request_too_large",
  MAX_USERS: "max_users_reached",
  UPSTREAM_RATE_LIMIT: "upstream_rate_limit",
  FASTLY_RATE_LIMIT: "fastly_rate_limit",
  REQUEST_BLOCKED: "request_blocked",
  INVALID_MODEL: "invalid_model",
  INVALID_REQUEST: "invalid_request",
  GLOBAL_BUDGET_EXCEEDED: "global_budget_exceeded",
  GUEST_BUDGET_EXCEEDED: "guest_budget_exceeded",
  MAX_GUESTS: "max_guests_reached",
  AUTH: "auth_error",
  CONTENT_EXTRACTION: "content_extraction_error",
  CANCELED: "canceled",
  INTERRUPTED: "interrupted",
  MODEL: "model_error",
  PROMPT_LOAD: "prompt_load_error",
  UNKNOWN: "unknown_error",
  ACTIVE_LIMIT: "active_limit_reached",
});

const MLPA_ERROR_CODES = new Map([
  [
    400,
    new Map([
      [8, MONITOR_ERROR_CODES.INVALID_MODEL],
      [9, MONITOR_ERROR_CODES.INVALID_REQUEST],
    ]),
  ],
  [
    403,
    new Map([
      [4, MONITOR_ERROR_CODES.MAX_USERS],
      [12, MONITOR_ERROR_CODES.MAX_GUESTS],
    ]),
  ],
  [413, new Map([[3, MONITOR_ERROR_CODES.REQUEST_TOO_LARGE]])],
  [
    429,
    new Map([
      [1, MONITOR_ERROR_CODES.BUDGET_EXCEEDED],
      [2, MONITOR_ERROR_CODES.RATE_LIMIT],
      [5, MONITOR_ERROR_CODES.UPSTREAM_RATE_LIMIT],
      [6, MONITOR_ERROR_CODES.FASTLY_RATE_LIMIT],
      [10, MONITOR_ERROR_CODES.GLOBAL_BUDGET_EXCEEDED],
      [11, MONITOR_ERROR_CODES.GUEST_BUDGET_EXCEEDED],
    ]),
  ],
  [500, new Map([[10, MONITOR_ERROR_CODES.GLOBAL_BUDGET_EXCEEDED]])],
]);

// Why a monitor paused itself. Stored on the monitor's expiry record.
export const MONITOR_EXPIRY_REASONS = Object.freeze({
  NO_MATCH: "no_match",
  MAX_AGE: "max_age",
});

// Reads each expiry rule's window in days from its pref.
const EXPIRY_RULE_DAYS = Object.freeze({
  [MONITOR_EXPIRY_REASONS.NO_MATCH]: () => lazy.expiryNoMatchDays,
  [MONITOR_EXPIRY_REASONS.MAX_AGE]: () => lazy.expiryMaxAgeDays,
});

/**
 * Error thrown by monitor runs that already know their failure category,
 * so it doesn't have to be guessed from the message afterwards.
 */
export class MonitorRunError extends Error {
  /**
   * @param {string} code - One of MONITOR_ERROR_CODES.
   * @param {string} message
   * @param {object} [options]
   * @param {Error} [options.cause] - The original error being wrapped, kept
   *   attached for debugging.
   */
  constructor(code, message, options) {
    super(message, options);
    this.name = "MonitorRunError";
    this.code = code;
  }
}

/**
 * Error thrown when creating or resuming a monitor would exceed
 * TOTAL_NUM_MONITORS active monitors. The UI keys off `code` rather than
 * the message.
 */
export class MonitorLimitError extends Error {
  /**
   * @param {number} limit - The active monitor cap that was hit.
   */
  constructor(limit) {
    super(`Cannot have more than ${limit} active monitors.`);
    this.name = "MonitorLimitError";
    this.code = MONITOR_ERROR_CODES.ACTIVE_LIMIT;
    this.limit = limit;
  }
}

// Fired once per monitor run that meets its condition, with the monitor id as
// the data. Unlike the desktop notification this is not suppressed by muting,
// it drives the passive dot on the monitor toolbar button.
export const MONITOR_CONDITION_MET_TOPIC = "smartwindow-monitor-condition-met";

// Fired once per monitor run that failed, with the monitor id as the data.
// Like the condition-met topic this is not suppressed by muting, it drives the
// passive dot on the monitor toolbar button.
export const MONITOR_RUN_FAILED_TOPIC = "smartwindow-monitor-run-failed";

// Scheduled runs starting at least this late record the "delayed" reason.
export const DELAYED_RUN_THRESHOLD_MS = 5 * 60 * 1000;

// Why a run happened, recorded as the telemetry reason on the run events.
export const RUN_REASONS = Object.freeze({
  MANUAL: "manual",
  TRIGGER_TIME: "trigger_time",
  DELAYED: "delayed",
});

const MONITOR_RESULT_SCHEMA = {
  type: "object",
  additionalProperties: false,
  required: ["explanation", "conditionMet"],
  properties: {
    explanation: { type: "string" },
    conditionMet: { type: "boolean" },
  },
};

/**
 * Individual monitor state and behavior.
 *
 * Monitors are responsible for running their individual monitor checks on a schedule,
 * invoking the language model with the monitor prompt and page content,
 * and recording the results in the monitor history.
 *
 * They are managed by Monitor Agent and they are a feature that allows a user to monitor
 * a set of URLs for changes or conditions and get notified when those conditions are met.
 */
export class Monitor {
  #timer = null;
  #running = false;
  #disposed = false;
  #abortController = null;
  #snapshotCapture = null;
  #snapshotAbortController = null;

  /**
   * @param {object} options
   * @param {string} [options.id] - Unique monitor ID.
   * @param {string} options.title - Title of the monitor, for display purposes.
   * @param {string} options.monitorPrompt - The user's monitoring prompt.
   * @param {string[]} options.watchUrls - URLs of the pages being monitored.
   * @param {Schedule} options.schedule - Schedule config.
   * @param {boolean} [options.enabled] - Whether the monitor should run.
   * @param {boolean} [options.notificationsMuted] - Whether desktop
   *   notifications are suppressed while the monitor keeps running.
   * @param {string} [options.createdAt] - Creation timestamp.
   * @param {string} [options.updatedAt] - Last update timestamp.
   * @param {string} [options.lastRunTime] - Last run timestamp.
   * @param {string} [options.nextRunTime] - Next run timestamp.
   * @param {string} [options.activeSince] - Start of the current active
   *   period, which the auto-expiry windows are measured from. Reset when the
   *   monitor is resumed or its definition is edited.
   * @param {string} [options.lastMatchAt] - Timestamp of the last run whose
   *   condition was met.
   * @param {{ expiredAt: string, reason: string }} [options.expiry] - Set
   *   when the monitor paused itself, with the MONITOR_EXPIRY_REASONS entry
   *   that applied.
   * @param {object[]} [options.history] - Saved monitor history entries.
   * @param {{ capturedAt: string, pageContent: string }} [options.initialSnapshot] -
   *   Page content extracted when the monitor was created, used as the
   *   baseline for change detection. The page content covers all watch URLs
   *   in the same concatenated format as run-time extraction.
   * @param {number} [options.runCount] - Lifetime number of runs started,
   *   surfaced as execution_seq in telemetry.
   */
  constructor({
    id = crypto.randomUUID(),
    title = "",
    monitorPrompt,
    watchUrls,
    schedule,
    enabled = true,
    notificationsMuted = false,
    createdAt,
    updatedAt,
    lastRunTime,
    nextRunTime,
    activeSince,
    lastMatchAt = null,
    expiry = null,
    history = [],
    initialSnapshot = null,
    runCount = 0,
  } = {}) {
    // validate the schedule is an actual schedule object and not a serialized object
    if (!schedule?.getNextRunTime) {
      throw new Error("Monitor schedule is invalid.");
    }

    const now = new Date().toISOString();
    createdAt ??= now;
    updatedAt ??= createdAt;
    lastRunTime ??= createdAt;
    activeSince ??= createdAt;

    this.id = id;
    this.title = String(title ?? "").trim();
    this.monitorPrompt = String(monitorPrompt ?? "").trim();
    this.watchUrls = trimAndFilterWatchUrls(watchUrls);
    this.schedule = schedule;
    this.enabled = enabled;
    this.notificationsMuted = !!notificationsMuted;
    this.createdAt = createdAt;
    this.updatedAt = updatedAt;
    this.lastRunTime = lastRunTime;
    this.nextRunTime =
      nextRunTime ?? schedule.getNextRunTime(lastRunTime).toISOString();
    this.activeSince = activeSince;
    this.lastMatchAt = lastMatchAt;
    this.expiry = expiry;
    this.history = Array.isArray(history) ? history : [];
    this.initialSnapshot = initialSnapshot;
    this.runCount = runCount;

    // final verification that the monitor is valid
    if (!this.id || !this.monitorPrompt || !this.watchUrls.length) {
      throw new Error("Monitor is invalid.");
    }
  }

  /**
   * @param {object} savedMonitor - Serialized monitor data.
   * @returns {Monitor}
   */
  static fromJSON(savedMonitor) {
    if (!savedMonitor?.id) {
      throw new Error("Monitor is invalid.");
    }

    const history = normalizeLoadedHistory(savedMonitor.history);
    return new Monitor({
      id: savedMonitor.id,
      title: savedMonitor.title,
      monitorPrompt: savedMonitor.monitorPrompt,
      watchUrls: savedMonitor.watchUrls,
      schedule: Schedule.fromJSON(savedMonitor.schedule),
      enabled: savedMonitor.enabled,
      notificationsMuted: savedMonitor.notificationsMuted,
      createdAt: savedMonitor.createdAt,
      updatedAt: savedMonitor.updatedAt,
      lastRunTime: savedMonitor.lastRunTime,
      nextRunTime: savedMonitor.nextRunTime,
      activeSince: savedMonitor.activeSince,
      // monitors stored before lastMatchAt existed fall back to their history
      lastMatchAt: savedMonitor.lastMatchAt ?? latestMatchTime(history),
      expiry: savedMonitor.expiry ?? null,
      history,
      initialSnapshot: savedMonitor.initialSnapshot ?? null,
      runCount: savedMonitor.runCount,
    });
  }

  /**
   * Run the monitor check when manually requested or when the scheduled time has come.
   *
   * @param {object} options
   * @param {boolean} [options.manual] - Whether this run was manually requested.
   * @param {number} [options.timeoutMs] - Run timeout.
   * @returns {Promise<void>}
   */
  async run({ manual = false, timeoutMs = MONITOR_RUN_TIMEOUT_MS } = {}) {
    // if the monitor is disposed or Smart Window is blocked, don't run it
    if (this.#disposed || lazy.MonitorAgent.isBlocked) {
      return;
    }

    // if the monitor is disabled or it is not time to run, don't run it unless it's a manual run
    const checkedAt = new Date();
    if (!manual) {
      if (!this.enabled || new Date(this.nextRunTime) > checkedAt) {
        this.scheduleNextRun();
        return;
      }
    }

    const delayMs = scheduledRunDelayMs({
      manual,
      nextRunTime: this.nextRunTime,
      checkedAt,
    });
    const reason = getRunReason(manual, delayMs);

    // make sure that a run is not already in progress, otherwise we would have overlapping runs
    // may happen with manual runs or if the previous run took longer than the schedule interval
    if (this.#running) {
      return;
    }

    // a scheduled run of a monitor that hit an auto-expiry rule pauses it
    // instead of checking; manual "check now" runs still go through
    if (!manual) {
      const expiryReason = this.getExpiryReason(checkedAt);
      if (expiryReason) {
        await lazy.MonitorAgent._expireMonitor(this, expiryReason);
        return;
      }
    }

    this.#running = true;
    const runStarted = ChromeUtils.now();
    const historyEntry = {
      id: crypto.randomUUID(),
      checkedAt: checkedAt.toISOString(),
      status: "running",
      resultExplanation: "",
      conditionMet: false,
    };
    this.addHistoryEntry(historyEntry);
    this.runCount += 1;
    this.updatedAt = checkedAt.toISOString();

    let result = null;
    let checkPromise = null;
    let timedOut = false;
    const runStats = {
      reason,
      executionSeq: this.runCount,
      model: null,
      modelLatencyMs: null,
    };
    const abortController = new AbortController();
    this.#abortController = abortController;
    recordRunRequest(this, { ...runStats, delayMs });
    try {
      // save the running state to disk
      await lazy.MonitorAgent._saveAndNotify(this);

      // run the monitor check with a timeout and abort signal
      checkPromise = this.runMonitorCheck({
        flowId: this.id,
        now: checkedAt,
        signal: abortController.signal,
        runStats,
      });

      // if the checkPromise times out, abort the monitor check and mark it as timed out
      result = await withTimeout(checkPromise, timeoutMs, () => {
        timedOut = true;
        abortController.abort(new Error("Monitor check timed out."));
      });

      // record the result of the monitor check in the history entry
      historyEntry.status = "success";
      historyEntry.resultExplanation = result.explanation;
      historyEntry.conditionMet = result.conditionMet;
      if (result.conditionMet) {
        this.lastMatchAt = historyEntry.checkedAt;
      }
    } catch (error) {
      historyEntry.status = "error";
      historyEntry.resultExplanation = error.message || String(error);
      historyEntry.errorCode = monitorErrorCode(error, timedOut);
    } finally {
      this.lastRunTime = checkedAt.toISOString();
      this.nextRunTime = this.schedule
        .getNextRunTime(this.lastRunTime)
        .toISOString();
      this.updatedAt = new Date().toISOString();

      // save the updated monitor state to disk and notify observers, even if the run failed or timed out
      try {
        await lazy.MonitorAgent._saveAndNotify(this);
      } finally {
        // if the run timed out, we need to make sure to abort the monitor check and finish the run
        if (timedOut && checkPromise) {
          checkPromise.catch(() => {});
        }
        this.#finishRun(abortController);
        recordRunEnd(this, {
          ...runStats,
          cancelCode: getCancelCode(
            abortController.signal.reason,
            historyEntry.errorCode
          ),
          failed: historyEntry.status === "error",
          errorCode: historyEntry.errorCode ?? null,
          outcome:
            historyEntry.status === "success"
              ? historyEntry.conditionMet
              : null,
          durationMs: Math.round(ChromeUtils.now() - runStarted),
          delayMs,
        });
      }
    }
  }

  /**
   * Run the monitor check by invoking the language model with the monitor prompt and the latest page content.
   *
   * @param {object} [options]
   * @param {string} [options.flowId] - Monitor conversation flow ID.
   * @param {Date} [options.now] - Monitor check time.
   * @param {AbortSignal} [options.signal] - Signal for monitor-layer aborts.
   * @param {{ reason: string, executionSeq: number, model: string|null, modelLatencyMs: number|null }} [options.runStats] -
   *   Telemetry context of the run; filled in with the model used and the
   *   model call time.
   * @returns {Promise<{ explanation: string, conditionMet: boolean }>}
   */
  async runMonitorCheck({
    flowId = null,
    now = new Date(),
    signal = null,
    runStats = null,
  } = {}) {
    throwIfAborted(signal);
    const conversation = await lazy.buildConversation(
      MODEL_FEATURES.AGENT_MONITOR,
      { flowId }
    );
    if (runStats) {
      runStats.model = conversation.engine?.model ?? null;
    }

    // Backfill the baseline for monitors without one (created before
    // snapshots existed, or the creation-time capture failed). The first
    // successful run then becomes the comparison baseline.
    let initialSnapshot = this.initialSnapshot;
    if (!initialSnapshot) {
      try {
        initialSnapshot = await this.ensureInitialSnapshot({
          conversation,
          signal,
        });
      } catch (error) {
        throwIfAborted(signal);
        lazy.log.warn(
          `Monitor check proceeding without a baseline snapshot: ${
            error.message ?? error
          }`
        );
        initialSnapshot = null;
      }
    }

    const pageContent = await extractMonitorPageContent(
      this.watchUrls,
      conversation,
      {
        signal,
      }
    );
    throwIfAborted(signal);

    let monitorAgentSystemPrompt, monitorAgentUserPrompt;
    try {
      [
        { prompt: monitorAgentSystemPrompt },
        { prompt: monitorAgentUserPrompt },
      ] = await Promise.all([
        lazy.loadPrompt(MODEL_FEATURES.AGENT_MONITOR, {
          module: "system-instructions",
        }),
        lazy.loadPrompt(MODEL_FEATURES.AGENT_MONITOR, { module: "user-data" }),
      ]);
    } catch (error) {
      throw new MonitorRunError(
        MONITOR_ERROR_CODES.PROMPT_LOAD,
        error.message || String(error),
        { cause: error }
      );
    }

    const systemPrompt = renderPrompt(monitorAgentSystemPrompt, {
      monitorPrompt: this.monitorPrompt,
    });
    const userPrompt = renderPrompt(monitorAgentUserPrompt, {
      pageUrls: this.watchUrls.join("\n - "),
      checkedAt: now.toISOString(),
      pageContent,
      snapshotCapturedAt: initialSnapshot?.capturedAt ?? "unavailable",
      snapshotContent:
        initialSnapshot?.pageContent ||
        "No initial snapshot is available for this monitor.",
    });

    // add messages to the conversation
    conversation.setSystemMessage(systemPrompt);
    conversation.addUserMessage(userPrompt);

    // run the conversation
    const token = await withAbortSignal(AuthProvider.getToken(), signal);
    throwIfAborted(signal);
    if (runStats) {
      recordRunStart(this, runStats);
    }
    const modelCallStarted = ChromeUtils.now();
    let response;
    try {
      response = await withAbortSignal(
        conversation.run({
          token,
          inferenceParams: {
            response_format: makeJSONSchemaBlob(
              "MonitorResult",
              MONITOR_RESULT_SCHEMA
            ),
          },
          tools: [],
        }),
        signal
      );
    } catch (error) {
      throwIfAborted(signal);
      // The failure happened in the inference call, so anything the message
      // sniffing can't place more precisely is a model error.
      const code = categorizeError(error);
      throw new MonitorRunError(
        code === MONITOR_ERROR_CODES.UNKNOWN ? MONITOR_ERROR_CODES.MODEL : code,
        error.message || String(error),
        { cause: error }
      );
    }
    if (runStats) {
      runStats.modelLatencyMs = Math.round(
        ChromeUtils.now() - modelCallStarted
      );
    }

    // parse and return the result
    throwIfAborted(signal);
    return this.parseMonitorResult(response);
  }

  /**
   * Extract the current page content of the watch URLs and store it as the
   * monitor's initial snapshot, replacing any previous one.
   *
   * The extraction runs through the same code path as run-time extraction so
   * the snapshot and later page content are directly comparable by the model.
   *
   * @param {object} [options]
   * @param {object} [options.conversation] - Existing conversation to reuse
   *   for the extraction; a fresh one is built when not provided.
   * @param {AbortSignal} [options.signal]
   * @returns {Promise<{ capturedAt: string, pageContent: string }>}
   */
  async captureInitialSnapshot({ conversation = null, signal = null } = {}) {
    throwIfAborted(signal);
    if (lazy.MonitorAgent.isBlocked) {
      throw new Error("Smart Window is blocked by AI Controls.");
    }
    // own controller so dispose() and cancelSnapshotCapture() can cancel a
    // capture that no caller waits on
    const abortController = new AbortController();
    this.#snapshotAbortController = abortController;
    const captureSignal = signal
      ? AbortSignal.any([signal, abortController.signal])
      : abortController.signal;
    const watchUrlsAtCapture = this.watchUrls;
    try {
      conversation ??= await lazy.buildConversation(
        MODEL_FEATURES.AGENT_MONITOR,
        { flowId: this.id }
      );
      const pageContent = await extractMonitorPageContent(
        watchUrlsAtCapture,
        conversation,
        { signal: captureSignal }
      );
      throwIfAborted(captureSignal);
      // An edit may have replaced the watch URLs while extraction ran; the
      // old pages must not become the baseline for the new ones.
      if (!urlListsEqual(this.watchUrls, watchUrlsAtCapture)) {
        throw new Error("Monitor watch URLs changed during snapshot capture.");
      }
      this.initialSnapshot = {
        capturedAt: new Date().toISOString(),
        pageContent,
      };
      return this.initialSnapshot;
    } finally {
      if (this.#snapshotAbortController === abortController) {
        this.#snapshotAbortController = null;
      }
    }
  }

  /**
   * Cancel any in-flight initial-snapshot capture, so a capture started for a
   * previous monitor definition can't become the baseline of the new one.
   * The next ensureInitialSnapshot() call starts a fresh capture.
   */
  cancelSnapshotCapture() {
    this.#snapshotAbortController?.abort(
      new Error("Monitor snapshot capture canceled by an edit.")
    );
    this.#snapshotCapture = null;
  }

  /**
   * Return the initial snapshot, capturing one first if the monitor doesn't
   * have one yet. Concurrent callers share a single in-flight capture.
   *
   * @param {object} [options]
   * @param {object} [options.conversation]
   * @param {AbortSignal} [options.signal]
   * @returns {Promise<{ capturedAt: string, pageContent: string }>}
   */
  async ensureInitialSnapshot({ conversation = null, signal = null } = {}) {
    if (this.initialSnapshot) {
      return this.initialSnapshot;
    }
    if (!this.#snapshotCapture) {
      const capture = this.captureInitialSnapshot({
        conversation,
        signal,
      }).finally(() => {
        // a canceled capture must not clear its replacement
        if (this.#snapshotCapture === capture) {
          this.#snapshotCapture = null;
        }
      });
      this.#snapshotCapture = capture;
    }
    return this.#snapshotCapture;
  }

  /**
   * Schedule the next run of the monitor based on the schedule and last run time
   * If the monitor is disabled or disposed, do not schedule a run.
   */
  scheduleNextRun() {
    this.clearTimer();
    if (this.#disposed || !this.enabled) {
      return;
    }

    // if nextRunTime is missing or invalid, compute it from lastRunTime and the schedule.
    let nextRun = this.nextRunTime ? new Date(this.nextRunTime) : null;
    if (!nextRun || Number.isNaN(nextRun.getTime())) {
      this.nextRunTime = this.schedule
        .getNextRunTime(this.lastRunTime)
        .toISOString();
      nextRun = new Date(this.nextRunTime);
    }

    this.#timer = new lazy.ScheduledTask(
      async () => {
        this.#timer = null;
        try {
          await this.run();
        } catch (error) {
          lazy.log.error(error);
        }
      },
      Math.max(Date.now() + 1000, nextRun.getTime())
    ).arm();
  }

  /**
   * Auto-expiry rule the monitor has hit, or null while it may keep running.
   * The maximum lifetime is measured from activeSince and the no-match window
   * from the later of activeSince and lastMatchAt. A rule whose pref is zero
   * or negative is disabled.
   *
   * @param {Date} [now]
   * @returns {string|null} One of MONITOR_EXPIRY_REASONS, or null.
   */
  getExpiryReason(now = new Date()) {
    const activeSince = Date.parse(this.activeSince);
    if (!Number.isFinite(activeSince)) {
      return null;
    }
    if (expiryRuleElapsed(MONITOR_EXPIRY_REASONS.MAX_AGE, activeSince, now)) {
      return MONITOR_EXPIRY_REASONS.MAX_AGE;
    }
    const lastMatch = Date.parse(this.lastMatchAt);
    const noMatchSince = Number.isFinite(lastMatch)
      ? Math.max(activeSince, lastMatch)
      : activeSince;
    if (expiryRuleElapsed(MONITOR_EXPIRY_REASONS.NO_MATCH, noMatchSince, now)) {
      return MONITOR_EXPIRY_REASONS.NO_MATCH;
    }
    return null;
  }

  /**
   * Parse the structured monitor result from a model response. Falls back to a
   * not-met result using the raw text when the JSON can't be parsed.
   *
   * @param {object} response
   * @returns {{ explanation: string, conditionMet: boolean }}
   */
  parseMonitorResult(response) {
    // extracts results if wrapped with ```json ``` code fences, otherwise returns the raw text
    const raw = response?.finalOutput?.trim() ?? "";
    const fenced = raw.match(/^```(?:json)?\s*([\s\S]*?)\s*```$/i);
    const payload = fenced ? fenced[1].trim() : raw;

    try {
      const parsed = JSON.parse(payload);
      if (
        !parsed ||
        typeof parsed !== "object" ||
        Array.isArray(parsed) ||
        typeof parsed.explanation !== "string" ||
        typeof parsed.conditionMet !== "boolean"
      ) {
        throw new Error("Invalid monitor result shape.");
      }

      return {
        explanation: parsed.explanation.trim(),
        conditionMet: parsed.conditionMet,
      };
    } catch {
      return {
        explanation:
          raw ||
          "The monitor check completed, but no model response was returned.",
        conditionMet: false,
      };
    }
  }

  /**
   * Plain, deeply-copied snapshot of the monitor for callers and observers.
   *
   * @returns {object}
   */
  toSerializable() {
    return {
      id: this.id,
      title: this.title,
      monitorPrompt: this.monitorPrompt,
      watchUrls: this.watchUrls.slice(),
      schedule: { ...this.schedule },
      enabled: this.enabled,
      notificationsMuted: this.notificationsMuted,
      createdAt: this.createdAt,
      updatedAt: this.updatedAt,
      lastRunTime: this.lastRunTime,
      nextRunTime: this.nextRunTime,
      activeSince: this.activeSince,
      lastMatchAt: this.lastMatchAt,
      expiry: this.expiry ? { ...this.expiry } : null,
      history: this.history.map(entry => ({ ...entry })),
      initialSnapshot: this.initialSnapshot
        ? { ...this.initialSnapshot }
        : null,
      runCount: this.runCount,
    };
  }

  clearTimer() {
    if (this.#timer) {
      this.#timer.disarm();
      this.#timer = null;
    }
  }

  /**
   * @param {string} [errorCode] - MONITOR_ERROR_CODES entry recorded on an
   *   in-flight run: CANCELED when the user tears the monitor down,
   *   INTERRUPTED for shutdown.
   */
  dispose(errorCode = MONITOR_ERROR_CODES.INTERRUPTED) {
    this.#disposed = true;
    this.#abortController?.abort(
      new MonitorRunError(
        errorCode,
        errorCode === MONITOR_ERROR_CODES.CANCELED
          ? "Monitor check was canceled before it finished."
          : "Monitor check was interrupted before it finished."
      )
    );
    this.#snapshotAbortController?.abort(
      new Error("Monitor snapshot capture aborted.")
    );
    this.clearTimer();
  }

  restore() {
    this.#disposed = false;
  }

  addHistoryEntry(entry) {
    this.history.push(entry);
    if (this.history.length > MAX_HISTORY_ENTRIES) {
      this.history.shift();
    }
  }

  #finishRun(abortController) {
    if (this.#abortController === abortController) {
      this.#abortController = null;
    }
    this.#running = false;
    if (!this.#disposed && this.enabled) {
      this.scheduleNextRun();
    }
  }
}

// time out and abort helpers for monitor runs

function throwIfAborted(signal) {
  if (!signal?.aborted) {
    return;
  }

  throw signal.reason instanceof Error
    ? signal.reason
    : new Error("Monitor check aborted.");
}

async function withAbortSignal(promise, signal) {
  if (!signal) {
    return promise;
  }

  throwIfAborted(signal);

  let onAbort;
  const abortPromise = new Promise((_resolve, reject) => {
    onAbort = () => {
      reject(
        signal.reason instanceof Error
          ? signal.reason
          : new Error("Monitor check aborted.")
      );
    };
    signal.addEventListener("abort", onAbort, { once: true });
  });

  try {
    return await Promise.race([promise, abortPromise]);
  } finally {
    signal.removeEventListener("abort", onAbort);
  }
}

async function withTimeout(promise, timeoutMs, onTimeout = null) {
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    return promise;
  }

  let timeoutId = null;
  const timeoutPromise = new Promise((_resolve, reject) => {
    timeoutId = lazy.setTimeout(() => {
      timeoutId = null;
      const error = new Error("Monitor check timed out.");
      error.name = "TimeoutError";
      onTimeout?.();
      reject(error);
    }, timeoutMs);
  });

  try {
    return await Promise.race([promise, timeoutPromise]);
  } finally {
    if (timeoutId) {
      lazy.clearTimeout(timeoutId);
    }
  }
}

function latestMatchTime(history) {
  return history.findLast(entry => entry?.conditionMet)?.checkedAt ?? null;
}

/**
 * Number of days after which an auto-expiry rule pauses a monitor. Zero or
 * negative means the rule is disabled, as does an unknown reason.
 *
 * @param {string} reason - One of MONITOR_EXPIRY_REASONS.
 * @returns {number}
 */
export function expiryRuleDays(reason) {
  const days = EXPIRY_RULE_DAYS[reason];
  if (!days) {
    lazy.log.error(`Unknown monitor expiry reason: ${reason}`);
    return 0;
  }
  return days();
}

function expiryRuleElapsed(reason, sinceMs, now) {
  const days = expiryRuleDays(reason);
  return days > 0 && now.getTime() - sinceMs >= days * DAY_MS;
}

// A "running" entry that survived to a reload means the run was interrupted
// (crash, shutdown) before it could finish, so reconcile it to an error.
function normalizeLoadedHistory(history) {
  if (!Array.isArray(history)) {
    return [];
  }
  return history.map(entry => {
    if (entry?.status !== "running") {
      return entry;
    }
    return {
      ...entry,
      status: "error",
      resultExplanation: "Monitor check was interrupted before it finished.",
      conditionMet: false,
      errorCode: MONITOR_ERROR_CODES.INTERRUPTED,
    };
  });
}

/**
 * Resolves the error code for a failed run. The caller's timed-out flag is
 * the most reliable signal so it wins over everything, including typed
 * errors that may have raced with the timeout abort. Typed errors then carry
 * their own code, abort wrappers are recognized by shape, and anything else
 * falls back to message-based categorization.
 *
 * @param {Error|string} error
 * @param {boolean} [timedOut]
 * @returns {string} One of MONITOR_ERROR_CODES.
 */
export function monitorErrorCode(error, timedOut = false) {
  if (timedOut || error?.name === "TimeoutError") {
    return MONITOR_ERROR_CODES.TIMEOUT;
  }
  if (error instanceof MonitorRunError) {
    return error.code;
  }
  if (error?.name === "AbortError" || /abort/i.test(error?.message ?? "")) {
    return MONITOR_ERROR_CODES.INTERRUPTED;
  }
  return categorizeError(error);
}

async function extractMonitorPageContent(
  urls,
  conversation,
  { signal = null } = {}
) {
  throwIfAborted(signal);
  const results = await withAbortSignal(
    lazy.GetPageContent.getPageContent(
      { url_list: urls, signal, settleDelayMs: pageSettleDelayMs },
      conversation
    ),
    signal
  );
  throwIfAborted(signal);

  // Partial failures still give the model something to check, but when no
  // page could be read (or, defensively, there were no pages at all) the run
  // must fail rather than report "not met".
  if (!results.some(result => result.ok)) {
    throw new MonitorRunError(
      MONITOR_ERROR_CODES.CONTENT_EXTRACTION,
      "None of the watched pages could be read, so this check did not run."
    );
  }

  return results
    .map(result => result.content)
    .join("\n\n<----- PAGE BREAK ---->\n\n");
}

export function urlListsEqual(a, b) {
  return a.length === b.length && a.every((url, index) => url === b[index]);
}

export function trimAndFilterWatchUrls(urls) {
  if (!Array.isArray(urls)) {
    return [];
  }
  if (urls.length > TOTAL_NUM_URLS_IN_MONITOR) {
    throw new Error(
      `Cannot watch more than ${TOTAL_NUM_URLS_IN_MONITOR} URLs in a monitor.`
    );
  }
  return urls.map(url => String(url ?? "").trim()).filter(isAllowedWatchUrl);
}

export function isAllowedWatchUrl(urlString) {
  const url = URL.parse(urlString);
  return !!url && ["http:", "https:"].includes(url.protocol);
}

function scheduledRunDelayMs({ manual, nextRunTime, checkedAt }) {
  if (manual) {
    return null;
  }
  const dueTime = Date.parse(nextRunTime);
  if (!Number.isFinite(dueTime)) {
    return null;
  }
  return Math.max(0, checkedAt.getTime() - dueTime);
}

function getRunReason(manual, delayMs) {
  if (manual) {
    return RUN_REASONS.MANUAL;
  }
  return delayMs !== null && delayMs >= DELAYED_RUN_THRESHOLD_MS
    ? RUN_REASONS.DELAYED
    : RUN_REASONS.TRIGGER_TIME;
}

// A run only counts as canceled when the abort that ended it is the same
// cancel or interrupt the history entry recorded. A dispose that lands after
// the check finished, during the final save, must not turn a completed run
// into a cancel.
function getCancelCode(abortReason, errorCode) {
  if (
    !(abortReason instanceof MonitorRunError) ||
    abortReason.code !== errorCode
  ) {
    return null;
  }
  return [
    MONITOR_ERROR_CODES.CANCELED,
    MONITOR_ERROR_CODES.INTERRUPTED,
  ].includes(abortReason.code)
    ? abortReason.code
    : null;
}

function buildRunTelemetryExtra(monitor, { reason, executionSeq }) {
  return {
    ...lazy.MonitorAgent._telemetryExtra(monitor),
    reason,
    execution_seq: executionSeq,
  };
}

function recordRunRequest(monitor, runStats) {
  const extra = buildRunTelemetryExtra(monitor, runStats);
  if (runStats.delayMs != null) {
    extra.delay = runStats.delayMs;
  }
  Glean.smartWindow.agenticActionExecuteRequest.record(extra);
}

function recordRunStart(monitor, runStats) {
  const extra = buildRunTelemetryExtra(monitor, runStats);
  if (runStats.model) {
    extra.model = runStats.model;
  }
  Glean.smartWindow.agenticActionExecuteStart.record(extra);
}

function recordRunEnd(
  monitor,
  {
    reason,
    executionSeq,
    cancelCode,
    failed,
    errorCode,
    outcome,
    durationMs,
    delayMs,
    model,
    modelLatencyMs,
  }
) {
  const extra = buildRunTelemetryExtra(monitor, { reason, executionSeq });
  extra.duration = durationMs;
  if (model) {
    extra.model = model;
  }
  if (cancelCode) {
    Glean.smartWindow.agenticActionExecuteCancel.record({
      ...extra,
      error_code: cancelCode,
    });
    return;
  }

  const completeExtra = { ...extra, success: !failed };
  if (failed && errorCode) {
    completeExtra.error_code = errorCode;
  }
  if (!failed && typeof outcome === "boolean") {
    completeExtra.outcome = outcome;
  }
  if (modelLatencyMs != null) {
    completeExtra.latency = modelLatencyMs;
  }
  if (delayMs != null) {
    completeExtra.delay = delayMs;
  }
  Glean.smartWindow.agenticActionExecuteComplete.record(completeExtra);
}

/**
 * @param {Monitor} monitor
 * @param {string} [since] - Timestamp to measure from, the creation time by
 *   default.
 * @returns {number} Milliseconds elapsed, 0 for an unparsable timestamp.
 */
export function monitorAgeMs(monitor, since = monitor.createdAt) {
  const start = Date.parse(since);
  return Number.isFinite(start) ? Math.max(0, Date.now() - start) : 0;
}

/**
 * Categorizes an error into a safe, predefined code for telemetry.
 *
 * Raw error messages are never returned to avoid accidentally including
 * sensitive information in telemetry.
 *
 * @param {Error|string} error - The error to categorize.
 * @returns {string} A safe telemetry error code.
 */
export function categorizeError(error) {
  const message = (error?.message ?? String(error)).toLowerCase();

  // MLPA subcodes identify failures that share an HTTP status.
  const status =
    typeof error?.status === "number"
      ? error.status
      : Number(message.match(/(\d{3}) status code/)?.[1]);
  const mlpaErrorCode = MLPA_ERROR_CODES.get(status)?.get(error?.error);
  if (mlpaErrorCode) {
    return mlpaErrorCode;
  }
  if (status === 406) {
    return MONITOR_ERROR_CODES.REQUEST_BLOCKED;
  }
  if (status === 413) {
    return MONITOR_ERROR_CODES.REQUEST_TOO_LARGE;
  }
  if (status === 400) {
    return MONITOR_ERROR_CODES.INVALID_REQUEST;
  }
  if (status === 429) {
    return MONITOR_ERROR_CODES.MODEL;
  }
  if (status === 401 || status === 403) {
    return MONITOR_ERROR_CODES.AUTH;
  }
  if (status === 408) {
    return MONITOR_ERROR_CODES.TIMEOUT;
  }
  if (status >= 500 && status <= 599) {
    return MONITOR_ERROR_CODES.MODEL;
  }

  const categories = [
    ["network_error", ["network", "fetch failed", "failed to connect"]],
    ["timeout", ["timeout"]],
    ["rate_limit", ["rate limit", "quota"]],
    ["auth_error", ["authentication", "unauthorized"]],
    ["content_extraction_error", ["extract", "parse"]],
    ["interrupted", ["interrupt", "abort"]],
    ["model_error", ["model api", "api error", "api endpoint"]],
  ];

  for (const [code, patterns] of categories) {
    if (patterns.some(pattern => message.includes(pattern))) {
      return code;
    }
  }

  return "unknown_error";
}
