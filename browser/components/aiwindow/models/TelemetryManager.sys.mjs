/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  TelemetryEngine:
    "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs",
  submitTelemetryResult:
    "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs",
  setInterval: "resource://gre/modules/Timer.sys.mjs",
  clearInterval: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({
    prefix: "LLMTelemetry",
    maxLogLevelPref: "browser.smartwindow.telemetryLogLevel",
  });
});

const LAST_RUN_PREF = "browser.smartwindow.lastLLMTelemetryRunTime";
const SCHEDULER_POLL_INTERVAL_MS = 2 * 60 * 1000;
const SCHEDULER_COOLDOWN_MS = 24 * 60 * 60 * 1000;

/**
 * Schedules periodic LLM-based telemetry jobs
 *
 */
export class TelemetryScheduler {
  #intervalHandle = 0;
  #destroyed = false;
  #running = false;

  static #instance = null;

  /**
   * Initializes the scheduler if the relevant pref is enabled.
   *
   * This should be called from startup/feature initialization code.
   *
   * @returns {TelemetryScheduler|null}
   *          The scheduler instance if initialized, otherwise null.
   */
  static maybeInit() {
    if (!this.#instance) {
      this.#instance = new TelemetryScheduler();
    }

    return this.#instance;
  }

  /**
   * Creates a new scheduler instance.
   *
   * The constructor:
   * - Starts the periodic interval timer.
   */
  constructor() {
    // run immediately (first run) or just start the interval.
    void this.#init();
    lazy.console.debug("Initialized");
  }

  /**
   * initializer for the telemetry scheduler.
   *
   * - Otherwise, starts the periodic interval timer.
   *
   * @private
   * @returns {Promise<void>}
   */
  async #init() {
    const isFirstRun = Services.prefs.getIntPref(LAST_RUN_PREF, 0) === 0; // back to ms

    if (isFirstRun) {
      lazy.console.debug("First run detected; running immediately.");
      // #onInterval's finally will start the interval
      await this.#onInterval();
    } else {
      this.#startInterval();
    }
  }

  /**
   * Starts the interval that periodically evaluates history drift and
   * potentially triggers memory generation.
   *
   * @throws {Error} If an interval is already running.
   */
  #startInterval() {
    if (this.#intervalHandle) {
      throw new Error(
        "Attempting to start an interval when one already existed"
      );
    }
    this.#intervalHandle = lazy.setInterval(
      this.#onInterval,
      SCHEDULER_POLL_INTERVAL_MS
    );
  }

  /**
   * Stops the currently running interval, if any.
   */
  #stopInterval() {
    if (this.#intervalHandle) {
      lazy.clearInterval(this.#intervalHandle);
      this.#intervalHandle = 0;
    }
  }
/**
   * Periodic interval handler.
   *
   * @private
   * @returns {Promise<void>} Resolves once the interval run completes.
   */
   #onInterval = async () => {
    if (this.#destroyed) {
      lazy.console.warn("Interval fired after destroy; ignoring.");
      return;
    }

    if (this.#running) {
      lazy.console.debug(
        "Skipping run because a previous run is still in progress."
      );
      return;
    }

    // check to see if the last run was less than SCHEDULER_COOLDOWN_MS - if yes, return
    const lastRun = Services.prefs.getIntPref(LAST_RUN_PREF, 0) * 1000; // back to ms
    if (Date.now() - lastRun < SCHEDULER_COOLDOWN_MS) { return; }

    this.#running = true;
    this.#stopInterval();

    try {
        // run telemetry
        const conversationsToRun = await lazy.ChatStore.getConversationsForTelemetry();
        const telemetryEngine = new lazy.TelemetryEngine();

        for ( const conversationObj of conversationsToRun ) {
            const telemetryNames = Object.keys(conversationObj.telemetryJobs);
            const conversation = await lazy.ChatStore.findConversationById(conversationObj.convId);
            const results = await telemetryEngine.runTelemetryByName(telemetryNames, conversation);
            lazy.submitTelemetryResult(
                    results,
                    conversationObj.convId,
                    conversationObj.modelId,
                    conversationObj.turnIndex,
                    "endOfConversation"
            );
            await lazy.ChatStore.markLLMTelemetryProcessed(
                conversationObj.convId,
                conversationObj.telemetryJobs,
                conversation.currentTurnIndex()
            );
        }
    } catch (error) {
        lazy.console.error("Failed to generate history memories", error);
    } finally {
        Services.prefs.setIntPref(LAST_RUN_PREF, Math.floor(Date.now() / 1000));
        if (!this.#destroyed) {
            this.#startInterval()
        };
        this.#running = false;
    }
   }
   
  destroy() {
    this.#stopInterval();
    this.#destroyed = true;
    TelemetryScheduler.#instance = null;
    lazy.console.debug("Destroyed");
  }
};