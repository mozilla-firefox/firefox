/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DAPReportController, Task } from "./DAPReportController.sys.mjs";

let lazy = {};
ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "DAPIncrementality",
    maxLogLevelPref: "toolkit.telemetry.dap.logLevel",
  });
});
ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
});

const DB_VERSION = 1;
const DB_NAME = "DAPIncrementality";
const REFERRER_STORE = "referrer";

export const DAPIncrementality = new (class {
  config = null;
  dapReportContoller = null;

  get db() {
    return this._db || (this._db = this.#createOrOpenDb());
  }

  async #createOrOpenDb() {
    try {
      return await this.#openDatabase();
    } catch {
      throw new Error("DAPIncrementality unable to load database.");
    }
  }

  async #openDatabase() {
    return await lazy.IndexedDB.open(DB_NAME, DB_VERSION, db => {
      if (!db.objectStoreNames.contains(REFERRER_STORE)) {
        db.createObjectStore(REFERRER_STORE, { keyPath: "taskId" });
      }
    });
  }

  async #recordReferrer(bucket) {
    try {
      const value = {
        taskId: this.config.taskId,
        bucket,
      };
      const tx = (await this.db).transaction(REFERRER_STORE, "readwrite");
      await tx.objectStore(REFERRER_STORE).put(value);
      await tx.done;
    } catch (err) {
      if (err.name === "NotFoundError") {
        lazy.logConsole.error(`Object store ${REFERRER_STORE} not found`);
      } else {
        lazy.logConsole.error("IndexedDB access error:", err);
      }
    }
  }

  async #deleteReferrer() {
    const tx = (await this.db).transaction([REFERRER_STORE], "readwrite");
    const referrerStore = tx.objectStore(REFERRER_STORE);
    referrerStore.delete(this.config.taskId);
    await tx.done;
  }

  async #getReferrer() {
    const tx = (await this.db).transaction(REFERRER_STORE, "readonly");
    const value = await tx.objectStore(REFERRER_STORE).get(this.config.taskId);
    await tx.done;
    return value;
  }

  async #processReferrerMeasurement(event) {
    let eventUrlProcessed = false;
    if (!event.hidden) {
      for (const pattern of this.config.targetUrlPatterns) {
        if (pattern.matches(event.url)) {
          eventUrlProcessed = true;
          const referrer = await this.#getReferrer();

          let success = false;
          if (referrer === undefined) {
            if (this.config.unknownReferrerBucket != null) {
              await this.dapReportContoller.recordMeasurement(
                this.config.taskId,
                this.config.unknownReferrerBucket
              );
            }
          } else {
            success = await this.dapReportContoller.recordMeasurement(
              this.config.taskId,
              referrer.bucket
            );
            if (success) {
              await this.#deleteReferrer();
            }
          }
          break;
        }
      }
    }

    // Greedy matching
    if (!eventUrlProcessed) {
      for (const referrerUrlPattern of this.config.referrerUrlPatterns) {
        if (referrerUrlPattern.pattern.matches(event.url)) {
          if (
            event.transitionType == lazy.PlacesUtils.history.TRANSITIONS.TYPED
          ) {
            await this.#recordReferrer(referrerUrlPattern.bucket);
          }
          break;
        }
      }
    }
  }

  async #processUrlVisit(event) {
    if (!event.hidden) {
      for (const visitUrlPattern of this.config.visitUrlPatterns) {
        if (visitUrlPattern.pattern.matches(event.url)) {
          await this.dapReportContoller.recordMeasurement(
            this.config.taskId,
            visitUrlPattern.bucket
          );
          break;
        }
      }
    }
  }

  async startup() {
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      lazy.logConsole.warn(
        "DAPIncrementality startup not possible due to shutdown."
      );
      return;
    }

    const placesTypes = ["page-visited", "history-cleared", "page-removed"];
    const placesListener = async events => {
      for (const event of events) {
        // Prioritizing data deletion.
        switch (event.type) {
          case "history-cleared":
          case "page-removed": {
            await this.#deleteReferrer();
            await this.dapReportContoller.deleteState();
            break;
          }
          case "page-visited": {
            const measurementType =
              lazy.NimbusFeatures.dapIncrementality.getVariable(
                "measurementType"
              ) || "";

            // Only one type of measurement can be configured for an experiment.
            const handlers = {
              visitMeasurement: () => this.#processUrlVisit(event),
              referrerMeasurement: () =>
                this.#processReferrerMeasurement(event),
            };

            (
              (await handlers[measurementType]) ||
              (() => {
                lazy.logConsole.debug(
                  `No handler for measurementType "${measurementType}"`
                );
              })
            )();
          }
        }
      }
    };

    lazy.NimbusFeatures.dapIncrementality.onUpdate(async () => {
      if (this.config !== null) {
        await this.dapReportContoller.cleanup(30 * 1000, "nimbus-update");
        await this.#deleteReferrer();

        this.config = null;
        this.dapReportContoller = null;
      }

      // Clear registered callbacks
      lazy.PlacesUtils.observers.removeListener(placesTypes, placesListener);

      // If we have an active Nimbus configuration, register this experiment.
      const measurementType =
        lazy.NimbusFeatures.dapIncrementality.getVariable("measurementType") ||
        "";

      if (
        ["visitMeasurement", "referrerMeasurement"].includes(measurementType)
      ) {
        this.initialize();
        lazy.PlacesUtils.observers.addListener(placesTypes, placesListener);

        let tasks = {};
        const task = new Task({
          taskId: this.config.taskId,
          bits: 8,
          vdaf: "histogram",
          length: this.config.length,
          timePrecision: this.config.timePrecision,
          defaultMeasurement: 0,
        });

        tasks[this.config.taskId] = task;
        this.dapReportContoller = new DAPReportController({
          tasks,
          options: {
            windowDays: 7,
            submissionIntervalMins: 240,
          },
        });
        this.dapReportContoller.startTimedSubmission();
      }
    });
  }

  initialize() {
    lazy.logConsole.debug("...Initialize experiment");
    let taskId = lazy.NimbusFeatures.dapIncrementality.getVariable("taskId");
    let length = lazy.NimbusFeatures.dapIncrementality.getVariable("length");
    let timePrecision =
      lazy.NimbusFeatures.dapIncrementality.getVariable("timePrecision");
    let unknownReferrerBucket =
      lazy.NimbusFeatures.dapIncrementality.getVariable(
        "unknownReferrerBucket"
      );

    this.config = {
      taskId,
      length,
      timePrecision,
      visitUrlPatterns: [],
      referrerUrlPatterns: [],
      targetUrlPatterns: [],
      unknownReferrerBucket,
    };

    let referrerUrls =
      lazy.NimbusFeatures.dapIncrementality.getVariable("referrerUrls") || "";

    for (const referrerUrl of referrerUrls) {
      let mpattern = new MatchPattern(referrerUrl.url);
      this.config.referrerUrlPatterns.push({
        pattern: mpattern,
        bucket: referrerUrl.bucket,
      });
    }

    let targetUrlsString =
      lazy.NimbusFeatures.dapIncrementality.getVariable("targetUrls") || "";
    const targetUrls = targetUrlsString.split(",").filter(Boolean);

    for (const url of targetUrls) {
      let mpattern = new MatchPattern(url);
      this.config.targetUrlPatterns.push(mpattern);
    }

    let visitCountUrls =
      lazy.NimbusFeatures.dapIncrementality.getVariable("visitCountUrls") || "";

    for (const visitCountUrl of visitCountUrls) {
      let mpattern = new MatchPattern(visitCountUrl.url);
      this.config.visitUrlPatterns.push({
        pattern: mpattern,
        bucket: visitCountUrl.bucket,
      });
    }
  }
})();
