/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DAPTelemetrySender } from "./DAPTelemetrySender.sys.mjs";

let lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "DAPVisitCounter",
    maxLogLevelPref: "toolkit.telemetry.dap.logLevel",
  });
});
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
});

const MAX_REPORTS = 1;
const MAX_VISIT_COUNT = 1;
const DAY_IN_MILLI = 1000 * 60 * 60 * 24;
const CONVERSION_RESET_MILLI = 7 * DAY_IN_MILLI;
const BUDGET_STORE = "budgets";
const DB_NAME = "ReportCounter";
const DB_VERSION = 1;

export const DAPVisitCounter = new (class {
  counters = null;
  timerId = null;

  get db() {
    return this._db || (this._db = this.createOrOpenDb());
  }

  async createOrOpenDb() {
    try {
      return await this.openDatabase();
    } catch {
      throw new Error("DAPVisitCounter unable to load database.");
    }
  }

  async openDatabase() {
    return await lazy.IndexedDB.open(DB_NAME, DB_VERSION, db => {
      if (!db.objectStoreNames.contains(BUDGET_STORE)) {
        db.createObjectStore(BUDGET_STORE);
      }
    });
  }

  async getBudgetStore() {
    return await this.getStore(BUDGET_STORE);
  }

  async clearBudgetStore() {
    const budgetStore = await this.getBudgetStore();
    await budgetStore.clear();
  }

  async getStore(storeName) {
    return (await this.db).objectStore(storeName, "readwrite");
  }

  async getBudget(task) {
    const now = Date.now();
    const budgetStore = await this.getBudgetStore();
    const budget = await budgetStore.get(task);

    if (!budget || now > budget.nextReset) {
      return {
        reportCount: 0,
        nextReset: now + CONVERSION_RESET_MILLI,
      };
    }

    return budget;
  }

  async updateBudget(budget, value, task) {
    const budgetStore = await this.getBudgetStore();
    budget.reportCount += Math.min(budget.reportCount + 1, MAX_REPORTS);
    await budgetStore.put(budget, task);
  }

  async startup() {
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      lazy.logConsole.warn(
        "DAPVisitCounter startup not possible due to shutdown."
      );
      return;
    }

    const asyncShutdownBlocker = async () => {
      lazy.logConsole.debug(`Sending on shutdown.`);
      await this.send(2 * 1000, "shutdown");
    };

    const placesTypes = ["page-visited"];
    const placesListener = events => {
      // Even using the event.hidden flag there mayb be some double counting
      // here. It would have to be fixed in the Places API.
      for (const event of events) {
        lazy.logConsole.debug(`Visited: ${event.url}`);
        if (event.hidden) {
          continue;
        }
        for (const counter of this.counters) {
          for (const pattern of counter.patterns) {
            if (pattern.matches(event.url)) {
              lazy.logConsole.debug(`${pattern.pattern} matched!`);
              counter.count = Math.min(counter.count + 1, MAX_VISIT_COUNT);
            }
          }
        }
      }
    };

    lazy.NimbusFeatures.dapTelemetry.onUpdate(async () => {
      // Cancel submission timer
      lazy.clearTimeout(this.timerId);
      this.timerId = null;

      // Flush data when changing enrollment status
      if (this.counters !== null) {
        await this.send(30 * 1000, "nimbus-update");
        this.counters = null;
      }

      // Clear registered calllbacks
      lazy.PlacesUtils.observers.removeListener(placesTypes, placesListener);
      lazy.AsyncShutdown.appShutdownConfirmed.removeBlocker(
        asyncShutdownBlocker
      );

      // If we have an active Nimbus configuration, register this DAPVisitCounter.
      if (
        lazy.NimbusFeatures.dapTelemetry.getVariable("enabled") &&
        lazy.NimbusFeatures.dapTelemetry.getVariable("visitCountingEnabled")
      ) {
        this.initialize_counters();

        lazy.PlacesUtils.observers.addListener(placesTypes, placesListener);

        lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
          "DAPVisitCounter: sending data",
          asyncShutdownBlocker
        );

        this.timerId = lazy.setTimeout(
          () => this.timed_send(),
          this.timeout_value()
        );
      } else {
        await this.clearBudgetStore();
      }
    });
  }

  initialize_counters() {
    let experiments = lazy.NimbusFeatures.dapTelemetry.getVariable(
      "visitCountingExperimentList"
    );

    this.counters = [];
    // This allows two different formats for distributing the URLs for the
    // experiment. The experiments get quite large and over 4096 bytes they
    // result in a warning (when mirrored in a pref as in this case).
    if (Array.isArray(experiments)) {
      for (const experiment of experiments) {
        let counter = { experiment, count: 0, patterns: [] };
        this.counters.push(counter);
        for (const url of experiment.urls) {
          let mpattern = new MatchPattern(url);
          counter.patterns.push(mpattern);
        }
      }
    } else {
      for (const [task, urls] of Object.entries(experiments)) {
        for (const [idx, url] of urls.entries()) {
          const fullUrl = `*://${url}/*`;

          this.counters.push({
            experiment: {
              task_id: task,
              task_veclen: 20,
              bucket: idx,
            },
            count: 0,
            patterns: [new MatchPattern(fullUrl)],
          });
        }
      }
    }
  }

  async timed_send() {
    lazy.logConsole.debug("Sending on timer.");
    await this.send(30 * 1000, "periodic");
    this.timerId = lazy.setTimeout(
      () => this.timed_send(),
      this.timeout_value()
    );
  }

  timeout_value() {
    const MINUTE = 60 * 1000;
    return MINUTE * (9 + Math.random() * 2); // 9 - 11 minutes
  }

  async send(timeout, reason) {
    if (!Array.isArray(this.counters)) {
      return;
    }

    let collected_measurements = new Map();
    for (const counter of this.counters) {
      if (!collected_measurements.has(counter.experiment.task_id)) {
        collected_measurements.set(
          counter.experiment.task_id,
          new Array(counter.experiment.task_veclen).fill(0)
        );
      }
      collected_measurements.get(counter.experiment.task_id)[
        counter.experiment.bucket
      ] = counter.count;
      counter.count = 0;
    }

    let send_promises = [];
    for (const [task_id, measurement] of collected_measurements) {
      // Check if this is a non-zero report, zero reports are always sent.
      if (measurement.some(num => num !== 0)) {
        // Retrieve the budget for this task and check if we are within the budget.
        const budget = await this.getBudget(task_id);

        if (budget.reportCount >= MAX_REPORTS) {
          // Already reached the budget, do not send the non-zero report.
          continue;
        } else {
          await this.updateBudget(budget, 1, task_id);
        }
      }

      let task = {
        id: task_id,
        vdaf: "sumvec",
        bits: 8,
        length: 20,
        time_precision: 60,
      };

      send_promises.push(
        DAPTelemetrySender.sendDAPMeasurement(task, measurement, {
          timeout,
          reason,
        })
      );
    }

    try {
      await Promise.all(send_promises);
    } catch (e) {
      lazy.logConsole.error("Failed to send report: ", e);
    }
  }

  show() {
    for (const counter of this.counters) {
      lazy.logConsole.info(
        `Experiment: ${counter.experiment.url} -> ${counter.count}`
      );
    }
    return this.counters;
  }
})();
