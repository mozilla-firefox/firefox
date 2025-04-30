/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * vim: sw=2 ts=2 sts=2 expandtab
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This component handles frecency recalculations and decay on idle.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", function () {
  return lazy.PlacesUtils.getLogger({ prefix: "FrecencyRecalculator" });
});

const MILLIS_PER_DAY = 86400000;

// Decay rate applied daily to frecency scores.
// A scaling factor of .975 results in an half-life of 28 days.
const FRECENCY_DECAYRATE = "0.975";
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "frecencyDecayRate",
  "places.frecency.decayRate",
  FRECENCY_DECAYRATE,
  null,
  val => {
    if (typeof val == "string") {
      val = parseFloat(val);
    }
    if (val > 1.0) {
      lazy.logger.error("Invalid frecency decay rate value: " + val);
      val = parseFloat(FRECENCY_DECAYRATE);
    }
    return val;
  }
);

// An adaptive history entry is removed if unused for these many days.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "adaptiveHistoryExpireDays",
  "places.adaptiveHistory.expireDays",
  90
);

// For origins frecency calculation only sample pages visited recently.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "originsFrecencyCutOffDays",
  "places.frecency.originsCutOffDays",
  90
);

// This pref stores whether recalculation should be faster.
// It is set when we detect that a lot of changes happened recently, and it
// will survive restarts. Once there's nothing left to recalculate, we unset
// the pref and return to the normal recalculation rate.
// Note this getter transforms the boolean pref value into an integer
// acceleration rate.
const PREF_ACCELERATE_RECALCULATION = "places.frecency.accelerateRecalculation";
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "accelerationRate",
  PREF_ACCELERATE_RECALCULATION,
  false,
  null,
  accelerate => (accelerate ? 2 : 1)
);

// Time between deferred task executions.
const DEFERRED_TASK_INTERVAL_MS = 2 * 60000;
// Maximum time to wait for an idle before the task is executed anyway.
const DEFERRED_TASK_MAX_IDLE_WAIT_MS = 5 * 60000;
// Number of entries to update at once.
const DEFAULT_CHUNK_SIZE = 50;
// Threshold used to evaluate whether the number of Places events from the last
// recalculation is high enough to deserve a recalculation rate increase.
const ACCELERATION_EVENTS_THRESHOLD = 250;

/**
 * Recalculates and decays frecency scores in Places.
 */
export class PlacesFrecencyRecalculator {
  classID = Components.ID("1141fd31-4c1a-48eb-8f1a-2f05fad94085");

  /**
   * A DeferredTask that runs our tasks.
   */
  #task = null;

  /**
   * Handler for alternative frecency.
   * This allows to manager alternative ranking algorithms to experiment with.
   */
  #alternativeFrecencyHelper = null;

  /**
   * Tracks whether the recalculator was finalized, usually due to shutdown.
   * We use this explicit boolean rather than checking for a null `#task`
   * because, due to async behavior, `#task` could be resurrected by
   * `#createOrUpdateTask`.
   */
  #finalized = false;

  /**
   * This is useful for testing.
   */
  get alternativeFrecencyInfo() {
    return this.#alternativeFrecencyHelper?.sets;
  }

  constructor() {
    lazy.logger.trace("Initializing Frecency Recalculator");

    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);

    // Do not initialize during shutdown.
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      this.#finalized = true;
      return;
    }

    this.#createOrUpdateTask();

    lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
      "PlacesFrecencyRecalculator: shutdown",
      () => this.#finalize()
    );

    // The public methods and properties are intended to be used by tests, and
    // are exposed through the raw js object. Since this is expected to work
    // based on signals or notification, it should not be necessary to expose
    // any API for the product, though if that would become necessary in the
    // future, we could add an interface for the service.
    this.wrappedJSObject = this;
    // This can be used by tests to await for the decay process.
    this.pendingFrecencyDecayPromise = Promise.resolve();
    this.pendingOriginsDecayPromise = Promise.resolve();

    Services.obs.addObserver(this, "idle-daily", true);
    Services.obs.addObserver(this, "frecency-recalculation-needed", true);

    this.#alternativeFrecencyHelper = new AlternativeFrecencyHelper(this);

    // Run once on startup, so we pick up any leftover work.
    lazy.PlacesUtils.history.shouldStartFrecencyRecalculation = true;
    this.maybeStartFrecencyRecalculation();
  }

  #createOrUpdateTask() {
    if (this.#finalized) {
      lazy.logger.trace(`Not resurrecting #task because finalized`);
      return;
    }
    let wasArmed = this.#task?.isArmed;
    if (this.#task) {
      this.#task.disarm();
      this.#task.finalize().catch(console.error);
    }
    this.#task = new lazy.DeferredTask(
      this.#taskFn.bind(this),
      DEFERRED_TASK_INTERVAL_MS / lazy.accelerationRate,
      DEFERRED_TASK_MAX_IDLE_WAIT_MS / lazy.accelerationRate
    );
    if (wasArmed) {
      this.#task.arm();
    }
  }

  async #taskFn() {
    if (this.#task.isFinalized) {
      return;
    }
    let timerId = Glean.places.frecencyRecalcChunkTime.start();
    try {
      if (await this.recalculateSomeFrecencies()) {
        Glean.places.frecencyRecalcChunkTime.stopAndAccumulate(timerId);
      } else {
        Glean.places.frecencyRecalcChunkTime.cancel(timerId);
      }
    } catch (ex) {
      Glean.places.frecencyRecalcChunkTime.cancel(timerId);
      console.error(ex);
      lazy.logger.error(ex);
    }
  }

  #finalize() {
    lazy.logger.trace("Finalizing frecency recalculator");
    // We don't mind about tasks completiion, since we can execute them in the
    // next session.
    this.#task.disarm();
    this.#task.finalize().catch(console.error);
    this.#finalized = true;
  }

  #lastEventsCount = 0;

  /**
   * Evaluates whether recalculation speed should be increased, and eventually
   * accelerates.
   *
   * @returns {boolean} whether the recalculation rate is increased.
   */
  maybeUpdateRecalculationSpeed() {
    if (lazy.accelerationRate > 1) {
      return true;
    }
    // We mostly care about additions to cover the common case of importing
    // bookmarks or history. We may care about removals, but in most cases they
    // reduce the number of entries to recalculate.
    let eventsCount =
      PlacesObservers.counts.get("page-visited") +
      PlacesObservers.counts.get("bookmark-added");
    let accelerate =
      eventsCount - this.#lastEventsCount > ACCELERATION_EVENTS_THRESHOLD;
    if (accelerate) {
      Services.prefs.setBoolPref(PREF_ACCELERATE_RECALCULATION, true);
      this.#createOrUpdateTask();
    }
    this.#lastEventsCount = eventsCount;
    return accelerate;
  }

  #resetRecalculationSpeed() {
    if (lazy.accelerationRate > 1) {
      Services.prefs.clearUserPref(PREF_ACCELERATE_RECALCULATION);
      this.#createOrUpdateTask();
    }
  }

  /**
   * Updates a chunk of outdated frecency values. If there's more frecency
   * values to update at the end of the process, it may rearm the task.
   *
   * @param {object} [options]
   * @param {number?} [options.chunkSize] maximum number of entries to update at a time,
   *   set to -1 to update any entry.
   * @returns {Promise<boolean>} Whether any entry was recalculated.
   */
  async recalculateSomeFrecencies({ chunkSize = DEFAULT_CHUNK_SIZE } = {}) {
    // In case of acceleration we don't bump up the chunkSize to avoid issues
    // with slow disk systems.
    lazy.logger.trace(
      `Recalculate ${chunkSize >= 0 ? chunkSize : "infinite"} frecency values`
    );
    let affectedCount = 0;
    let hasRecalculatedAnything = false;
    let db = await lazy.PlacesUtils.promiseUnsafeWritableDBConnection();
    await db.executeTransaction(async function () {
      let affected = await db.executeCached(
        `UPDATE moz_places
        SET frecency = CALCULATE_FRECENCY(id)
        WHERE id IN (
          SELECT id FROM moz_places
          WHERE recalc_frecency = 1
          ORDER BY frecency DESC, visit_count DESC
          LIMIT ${chunkSize}
        )
        RETURNING id`
      );
      affectedCount += affected.length;
    });
    let shouldRestartRecalculation = affectedCount >= chunkSize;
    hasRecalculatedAnything = affectedCount > 0;
    if (hasRecalculatedAnything) {
      PlacesObservers.notifyListeners([new PlacesRanking()]);
    }

    // Also recalculate some origins frecency.
    affectedCount = await this.#recalculateSomeOriginsFrecencies({
      chunkSize,
    });
    shouldRestartRecalculation ||= affectedCount >= chunkSize;
    hasRecalculatedAnything ||= affectedCount > 0;

    // If alternative frecency is enabled, also recalculate a chunk of it.
    affectedCount =
      await this.#alternativeFrecencyHelper.recalculateSomeAlternativeFrecencies(
        { chunkSize }
      );
    shouldRestartRecalculation ||= affectedCount >= chunkSize;
    hasRecalculatedAnything ||= affectedCount > 0;

    if (chunkSize > 0 && shouldRestartRecalculation) {
      // There's more entries to recalculate, rearm the task.
      this.maybeUpdateRecalculationSpeed();
      this.#task.arm();
    } else {
      this.#resetRecalculationSpeed();
      // There's nothing left to recalculate, wait for the next change.
      lazy.PlacesUtils.history.shouldStartFrecencyRecalculation = false;
      this.#task.disarm();
    }
    return hasRecalculatedAnything;
  }

  async #recalculateSomeOriginsFrecencies({ chunkSize }) {
    lazy.logger.trace(`Recalculate ${chunkSize} origins frecency values`);
    let affectedCount = 0;
    let db = await lazy.PlacesUtils.promiseUnsafeWritableDBConnection();
    await db.executeTransaction(async () => {
      // NULL frecencies are normalized to 1.0 (to avoid confusion with pages
      // 0 frecency special meaning), as the table doesn't support NULL values.
      let affected = await db.executeCached(
        `
        UPDATE moz_origins
        SET frecency = IFNULL((
          SELECT sum(frecency)
          FROM moz_places h
          WHERE origin_id = moz_origins.id
          AND last_visit_date >
            strftime('%s','now','localtime','start of day',
                     '-${lazy.originsFrecencyCutOffDays} day','utc') * 1000000
        ), 1.0), recalc_frecency = 0
        WHERE id IN (
          SELECT id FROM moz_origins
          WHERE recalc_frecency = 1
          ORDER BY frecency DESC
          LIMIT ${chunkSize}
        )
        RETURNING id`
      );
      affectedCount += affected.length;

      // Calculate and store the frecency threshold. Origins whose frecency is
      // above this value will be considered meaningful and autofilled.
      // While it may be tempting to do this only when some frecency was
      // updated, that won't catch the edge case of the moz_origins table being
      // emptied.
      // In case of NULL, the default threshold is 2, that is higher than the
      // default frecency set above.
      let threshold = (
        await db.executeCached(`SELECT avg(frecency) FROM moz_origins`)
      )[0].getResultByIndex(0);
      await lazy.PlacesUtils.metadata.set(
        "origin_frecency_threshold",
        threshold ?? 2
      );
    });

    return affectedCount;
  }

  /**
   * Forces full recalculation of any outdated frecency values.
   * This exists for testing purposes; in tests we don't want to wait for
   * the deferred task to run, this can enforce a recalculation.
   */
  async recalculateAnyOutdatedFrecencies() {
    this.#task.disarm();
    return this.recalculateSomeFrecencies({ chunkSize: -1 });
  }

  /**
   * Whether a recalculation task is pending.
   */
  get isRecalculationPending() {
    return this.#task.isArmed;
  }

  /**
   * Invoked periodically to eventually start a recalculation task.
   */
  maybeStartFrecencyRecalculation() {
    if (
      lazy.PlacesUtils.history.shouldStartFrecencyRecalculation &&
      !this.#task.isFinalized
    ) {
      lazy.logger.trace("Arm frecency recalculation");
      this.#task.arm();
    }
  }

  /**
   * Decays frecency and adaptive history.
   *
   * @returns {Promise<void>} once the process is complete. Never rejects.
   */
  async decay() {
    lazy.logger.trace("Decay frecency");
    let timerId = Glean.places.idleFrecencyDecayTime.start();
    // Ensure moz_places_afterupdate_frecency_trigger ignores decaying
    // frecency changes.
    lazy.PlacesUtils.history.isFrecencyDecaying = true;
    try {
      let db = await lazy.PlacesUtils.promiseUnsafeWritableDBConnection();
      await db.executeTransaction(async function () {
        // Decay all frecency rankings to reduce value of pages that haven't
        // been visited in a while.
        await db.executeCached(
          `UPDATE moz_places SET frecency = ROUND(frecency * :decay_rate)
            WHERE frecency > 0 AND recalc_frecency = 0`,
          { decay_rate: lazy.frecencyDecayRate }
        );
        // Decay potentially unused adaptive entries (e.g. those that are at 1)
        // to allow better chances for new entries that will start at 1.
        await db.executeCached(
          `UPDATE moz_inputhistory SET use_count = use_count * :decay_rate`,
          { decay_rate: lazy.frecencyDecayRate }
        );
        // Delete any adaptive entries that won't help in ordering anymore.
        await db.executeCached(
          `DELETE FROM moz_inputhistory WHERE use_count < :use_count`,
          {
            use_count: Math.pow(
              lazy.frecencyDecayRate,
              lazy.adaptiveHistoryExpireDays
            ),
          }
        );

        Glean.places.idleFrecencyDecayTime.stopAndAccumulate(timerId);
        PlacesObservers.notifyListeners([new PlacesRanking()]);
      });
    } catch (ex) {
      Glean.places.idleFrecencyDecayTime.cancel(timerId);
      console.error(ex);
      lazy.logger.error(ex);
    } finally {
      lazy.PlacesUtils.history.isFrecencyDecaying = false;
    }
  }

  /**
   * Mark frecency of origins that were not visited for some time to be
   * recalculated, otherwise they'd be stuck at the last calculated value.
   */
  async requestRecalcOfNotRecentlyVisitedOrigins() {
    // Recalculate every 7 days, as this is not urgent.
    const now = Date.now();
    const key = "origins_frecency_last_decay_timestamp";
    let lastRecalcTime = await lazy.PlacesUtils.metadata.get(key, now);
    if (lastRecalcTime > now - 7 * MILLIS_PER_DAY) {
      lazy.logger.trace("Skipping as not enough time passed");
      return;
    }
    await lazy.PlacesUtils.metadata.set(key, now);

    // To limit amount of work, only recalculate origins over the threshold.
    let threshold = await lazy.PlacesUtils.metadata.get(
      "origin_frecency_threshold",
      0
    );
    // This guessed threshold limit is just a fail-safe to avoid recalculating
    // the same value over and over if history gets disabled and the threshold
    // keeps getting smaller and smaller. At a certain point origin won't have
    // recent visits, and its frecency will be set to 1.
    if (threshold < 100) {
      lazy.logger.trace("Skipping as threshold too low");
      return;
    }

    lazy.logger.trace("Recalculate origins not recently visited");
    let db = await lazy.PlacesUtils.promiseUnsafeWritableDBConnection();
    await db.execute(
      `
      UPDATE moz_origins
      SET recalc_frecency = 1, recalc_alt_frecency = 1
      WHERE id IN (
        SELECT id
        FROM moz_origins
        WHERE frecency >= :threshold
        AND recalc_frecency = 0
        AND NOT EXISTS (
          SELECT 1 FROM moz_places
	        WHERE origin_id = moz_origins.id
	          AND (
              foreign_count > 0 OR
              last_visit_date > strftime('%s', 'now', '-' || :cutoff || ' days') * 1000000
            )
        )
      )
    `,
      { threshold, cutoff: lazy.originsFrecencyCutOffDays }
    );
  }

  observe(subject, topic) {
    lazy.logger.trace(`Got ${topic} topic`);
    if (this.#finalized) {
      lazy.logger.trace(`Ignoring topic because finalized`);
      return;
    }
    switch (topic) {
      case "idle-daily":
        this.pendingFrecencyDecayPromise = this.decay();
        // Also recalculate frecencies.
        lazy.logger.trace("Frecency recalculation on idle");
        lazy.PlacesUtils.history.shouldStartFrecencyRecalculation = true;
        this.maybeStartFrecencyRecalculation();
        // Recalc frecency of origins that were not visited for
        // some time, as otherwise they'd be stuck at the last calculated
        // value, influencing threshold.
        // TODO (Bug 1943104): we should replace frecency with an exponential
        // self-decaying value, so we don't need to recalculate these.
        this.pendingOriginsDecayPromise =
          this.requestRecalcOfNotRecentlyVisitedOrigins();
        return;
      case "frecency-recalculation-needed":
        lazy.logger.trace("Frecency recalculation requested");
        this.maybeUpdateRecalculationSpeed();
        this.maybeStartFrecencyRecalculation();
        return;
      case "test-execute-taskFn":
        subject.promise = this.#taskFn();
        return;
      case "test-alternative-frecency-init":
        this.#alternativeFrecencyHelper = new AlternativeFrecencyHelper(this);
        subject.promise =
          this.#alternativeFrecencyHelper.initializedDeferred.promise;
    }
  }
}

/**
 * Recalculates experimental alternative frecency scores.
 */
class AlternativeFrecencyHelper {
  initializedDeferred = Promise.withResolvers();
  #recalculator = null;

  sets = {
    pages: {
      // This pref is only read once and used to kick-off recalculations.
      enabled: lazy.PlacesUtils.history.isAlternativeFrecencyEnabled,
      // Key used to store variables in the moz_meta table.
      metadataKey: "page_alternative_frecency",
      // The table containing frecency.
      table: "moz_places",
      // Object containing variables influencing the calculation.
      // Any change to this object will cause a full recalculation on restart.
      variables: {
        // Current version of pages alternative frecency.
        //  ! IMPORTANT: Always bump up when making changes to the algorithm.
        version: 3,
        veryHighWeight: Services.prefs.getIntPref(
          "places.frecency.pages.alternative.veryHighWeight",
          200
        ),
        highWeight: Services.prefs.getIntPref(
          "places.frecency.pages.alternative.highWeight",
          100
        ),
        mediumWeight: Services.prefs.getIntPref(
          "places.frecency.pages.alternative.mediumWeight",
          50
        ),
        lowWeight: Services.prefs.getIntPref(
          "places.frecency.pages.alternative.lowWeight",
          20
        ),
        halfLifeDays: Services.prefs.getIntPref(
          "places.frecency.pages.alternative.halfLifeDays",
          30
        ),
        numSampledVisits: Services.prefs.getIntPref(
          "places.frecency.pages.alternative.numSampledVisits",
          10
        ),
      },
      method: this.#recalculateSomePagesAlternativeFrecencies,
    },

    origins: {
      // This pref is only read once and used to kick-off recalculations.
      enabled: Services.prefs.getBoolPref(
        "places.frecency.origins.alternative.featureGate",
        false
      ),
      // Key used to store variables in the moz_meta table.
      metadataKey: "origin_alternative_frecency",
      // The table containing frecency.
      table: "moz_origins",
      // Object containing variables influencing the calculation.
      // Any change to this object will cause a full recalculation on restart.
      variables: {
        // Current version of origins alternative frecency.
        //  ! IMPORTANT: Always bump up when making changes to the algorithm.
        version: 2,
        // Frecencies of pages are ignored after these many days.
        daysCutOff: Services.prefs.getIntPref(
          "places.frecency.origins.alternative.daysCutOff",
          90
        ),
      },
      method: this.#recalculateSomeOriginsAlternativeFrecencies,
    },
  };

  constructor(recalculator) {
    this.#recalculator = recalculator;
    this.#kickOffAlternativeFrecencies()
      .catch(console.error)
      .finally(() => this.initializedDeferred.resolve());
  }

  async #kickOffAlternativeFrecencies() {
    let recalculateFirstChunk = false;
    for (let [type, set] of Object.entries(this.sets)) {
      // Now check the variables cached in the moz_meta table. If not found we
      // assume alternative frecency was disabled in the previous session.
      let storedVariables = await lazy.PlacesUtils.metadata.get(
        set.metadataKey,
        null
      );

      // Check whether this is the first-run, that happens when the alternative
      // ranking is enabled and it was not at the previous session, or variables
      // were changed. We should recalculate all the alternative frecency values.
      if (
        set.enabled &&
        !lazy.ObjectUtils.deepEqual(set.variables, storedVariables)
      ) {
        lazy.logger.trace(
          `Alternative frecency of ${type} must be recalculated`
        );
        await lazy.PlacesUtils.withConnectionWrapper(
          `PlacesFrecencyRecalculator :: ${type} alternative frecency set recalc`,
          async db => {
            await db.execute(`UPDATE ${set.table} SET recalc_alt_frecency = 1`);
          }
        );
        await lazy.PlacesUtils.metadata.set(set.metadataKey, set.variables);
        recalculateFirstChunk = true;
        continue;
      }

      if (!set.enabled && storedVariables) {
        lazy.logger.trace(`Clean up alternative frecency of ${type}`);
        // Clear alternative frecency to save on space.
        await lazy.PlacesUtils.withConnectionWrapper(
          `PlacesFrecencyRecalculator :: ${type} alternative frecency set NULL`,
          async db => {
            await db.execute(`UPDATE ${set.table} SET alt_frecency = NULL`);
          }
        );
        await lazy.PlacesUtils.metadata.delete(set.metadataKey);
      }
    }

    if (recalculateFirstChunk) {
      // Do a first recalculation immediately, so we don't leave the user
      // with unranked entries for too long.
      await this.recalculateSomeAlternativeFrecencies();

      // Ensure the recalculation task is armed for a second run.
      lazy.PlacesUtils.history.shouldStartFrecencyRecalculation = true;
      this.#recalculator.maybeStartFrecencyRecalculation();
    }
  }

  /**
   * Updates a chunk of outdated frecency values.
   *
   * @param {object} [options]
   * @param {number} [options.chunkSize] maximum number of entries to update at a time,
   *   set to -1 to update any entry.
   * @returns {Promise<number>} Number of affected pages.
   */
  async recalculateSomeAlternativeFrecencies({
    chunkSize = DEFAULT_CHUNK_SIZE,
  } = {}) {
    let affected = 0;
    for (let set of Object.values(this.sets)) {
      if (!set.enabled) {
        continue;
      }
      try {
        affected += await set.method({ chunkSize, variables: set.variables });
      } catch (ex) {
        console.error(ex);
      }
    }
    return affected;
  }

  async #recalculateSomePagesAlternativeFrecencies({ chunkSize }) {
    lazy.logger.trace(
      `Recalculate ${chunkSize * 2} alternative pages frecency values`
    );
    // Since it takes a long period of time to recalculate frecency of all the
    // pages, due to the high number of them, we artificially increase the
    // chunk size here.
    let db = await lazy.PlacesUtils.promiseUnsafeWritableDBConnection();
    let affected = await db.executeCached(
      `UPDATE moz_places
       SET alt_frecency = CALCULATE_ALT_FRECENCY(moz_places.id),
           recalc_alt_frecency = 0
       WHERE id IN (
        SELECT id FROM moz_places
          WHERE recalc_alt_frecency = 1
          ORDER BY frecency DESC
          LIMIT ${chunkSize * 2}
      )
      RETURNING id`
    );
    return affected;
  }

  async #recalculateSomeOriginsAlternativeFrecencies({ chunkSize, variables }) {
    lazy.logger.trace(
      `Recalculate ${chunkSize} alternative origins frecency values`
    );
    let affectedCount = 0;
    let db = await lazy.PlacesUtils.promiseUnsafeWritableDBConnection();
    await db.executeTransaction(async () => {
      let affected = await db.executeCached(
        `
        UPDATE moz_origins
        SET alt_frecency = (
          SELECT sum(frecency)
          FROM moz_places h
          WHERE origin_id = moz_origins.id
          AND last_visit_date >
            strftime('%s','now','localtime','start of day',
                     '-${variables.daysCutOff} day','utc') * 1000000
        ), recalc_alt_frecency = 0
        WHERE id IN (
          SELECT id FROM moz_origins
          WHERE recalc_alt_frecency = 1
          ORDER BY frecency DESC
          LIMIT ${chunkSize}
        )
        RETURNING id`
      );
      affectedCount += affected.length;

      // Calculate and store the alternative frecency threshold. Origins above
      // this threshold will be considered meaningful and autofilled.
      if (affected.length) {
        let threshold = (
          await db.executeCached(`SELECT avg(alt_frecency) FROM moz_origins`)
        )[0].getResultByIndex(0);
        await lazy.PlacesUtils.metadata.set(
          "origin_alt_frecency_threshold",
          threshold
        );
      }
    });

    return affectedCount;
  }
}
