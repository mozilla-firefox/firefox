/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* The purpose of this class is to limit (cap) sending of DAP reports.
 * The current DAP draft standard is available here:
 * https://github.com/ietf-wg-ppm/draft-ietf-ppm-dap */

import { DAPTelemetrySender } from "./DAPTelemetrySender.sys.mjs";

const DAY_IN_MILLI = 1000 * 60 * 60 * 24;
const MINUTE_IN_MILLI = 60 * 1000;

let lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "DAPReportController",
    maxLogLevelPref: "toolkit.telemetry.dap.logLevel",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const DB_NAME = "SubmissionCap";
const DB_VERSION = 1;
const FREQ_CAP_STORE = "freq_caps";
const REPORT_STORE = "reports";

/**
 * Represents a DAP task configuration.
 */
export class Task {
  /**
   * Constructs a Task object representing a DAP task configuration.
   *
   * @param {object} params
   * @param {string} params.taskId - The unique identifier for the task.
   * @param {string} params.vdaf - The VDAF algorithm type (e.g., "sum", "sumvec", "histogram").
   * @param {number} params.bits - The number of bits for the measurement.
   * @param {number} params.length - The length of the measurement sumvec or histogram.
   * @param {number} params.timePrecision - The time precision for the task.
   * @param {number|Array<number>} params.defaultMeasurement - The default measurement value to use.
   */
  constructor({
    taskId,
    vdaf,
    bits,
    length,
    timePrecision,
    defaultMeasurement,
  }) {
    this._taskId = taskId;
    this._vdaf = vdaf;
    this._bits = bits;
    this._length = length;
    this._timePrecision = timePrecision;
    this._defaultMeasurement = defaultMeasurement;
  }
}

/**
 * @typedef {object} DAPReportControllerOptions
 * @property {number} windowDays - The number of days before resetting the frequency cap.
 * @property {number} submissionIntervalMins - The interval in minutes between automatic submissions.
 */

/**
 * Manages DAP report submission with frequency capping.
 */
export class DAPReportController {
  /**
   * Constructs a DAPReportController to manage DAP report submission with frequency capping.
   *
   * @param {object} params
   * @param {object} params.tasks - Map of task IDs to Task objects.
   * @param {DAPReportControllerOptions} params.options - Configuration options for the controller.
   * @param {Function} params.DateNowFn - Function to get the current time in milliseconds (defaults to Date.now).
   */
  constructor({ tasks, options, DateNowFn = Date.now } = {}) {
    this._tasks = tasks;
    this._windowDays = options.windowDays;
    this._submissionTimerMins = options.submissionIntervalMins;
    this._timerId = null;
    this._now = DateNowFn;
  }

  get db() {
    return this._db || (this._db = this.#createOrOpenDb());
  }

  async #createOrOpenDb() {
    try {
      return await this.#openDatabase();
    } catch {
      throw new Error("DAPReportController unable to load database.");
    }
  }

  async #openDatabase() {
    return await lazy.IndexedDB.open(DB_NAME, DB_VERSION, db => {
      if (!db.objectStoreNames.contains(FREQ_CAP_STORE)) {
        db.createObjectStore(FREQ_CAP_STORE, { keyPath: "taskId" });
      }
      if (!db.objectStoreNames.contains(REPORT_STORE)) {
        db.createObjectStore(REPORT_STORE, { keyPath: "taskId" });
      }
    });
  }

  /* Clears a pending report and updates the freq cap data
   */
  async #releasePendingReport(report) {
    let cap = {
      taskId: report.taskId,
      nextReset: this._now() + this._windowDays * DAY_IN_MILLI,
    };

    const tx = (await this.db).transaction(
      [REPORT_STORE, FREQ_CAP_STORE],
      "readwrite"
    );

    const reportStore = tx.objectStore(REPORT_STORE);
    await reportStore.delete(report.taskId);
    const capStore = tx.objectStore(FREQ_CAP_STORE);
    await capStore.put(cap);
    await tx.done;
  }

  async getFreqCap(taskId) {
    const tx = (await this.db).transaction(FREQ_CAP_STORE, "readonly");
    const cap = await tx.objectStore(FREQ_CAP_STORE).get(taskId);
    await tx.done;
    return cap;
  }

  async recordMeasurement(taskId, measurement) {
    try {
      const cap = await this.getFreqCap(taskId);

      if (cap === undefined || cap.nextReset < this._now()) {
        const taskMetaData = this._tasks[taskId];
        const report = {
          taskId,
          vdaf: taskMetaData._vdaf,
          bits: taskMetaData._bits,
          length: taskMetaData._length,
          measurement,
        };
        const tx = (await this.db).transaction(REPORT_STORE, "readwrite");
        await tx.objectStore(REPORT_STORE).put(report);
        await tx.done;
        return true;
      }
      lazy.logConsole.debug(`reached cap, nextReset: ${cap.nextReset}`);
    } catch (err) {
      if (err.name === "NotFoundError") {
        console.error(
          `Object store ${FREQ_CAP_STORE} or ${REPORT_STORE} not found`
        );
      } else {
        console.error("IndexedDB access error:", err);
      }
    }
    return false;
  }

  /* Deletes any pending report or freq cap data from DB
   */
  async deleteState() {
    const taskIds = Object.keys(this._tasks);
    const tx = (await this.db).transaction(
      [REPORT_STORE, FREQ_CAP_STORE],
      "readwrite"
    );
    const reportStore = tx.objectStore(REPORT_STORE);
    for (const taskId of taskIds) {
      reportStore.delete(taskId);
    }

    const capStore = tx.objectStore(FREQ_CAP_STORE);
    for (const taskId of taskIds) {
      capStore.delete(taskId);
    }
    await tx.done;
  }

  /* Shutdowns DAPReportCollector, sends any pending reports
   * and deletes any pending report or freq cap data from DB
   */
  async cleanup(timeout, reason) {
    lazy.clearTimeout(this._timerId);
    await this.submit(timeout, reason);
    await this.deleteState();
  }

  async startTimedSubmission() {
    await this.submit(30 * 1000, "periodic");
    this._timerId = lazy.setTimeout(
      () => this.startTimedSubmission(),
      this.#timeoutValue()
    );
  }
  #timeoutValue() {
    return this._submissionTimerMins * MINUTE_IN_MILLI;
  }

  async getReportToSubmit(taskId) {
    const tx = (await this.db).transaction(REPORT_STORE, "readonly");
    const report = await tx.objectStore(REPORT_STORE).get(taskId);
    await tx.done;
    return report;
  }

  async submit(timeout, reason) {
    let sendPromises = [];

    for (const [taskId, metadata] of Object.entries(this._tasks)) {
      let task = {
        id: taskId,
        vdaf: metadata._vdaf,
        bits: metadata._bits,
        length: metadata._length,
        time_precision: metadata._timePrecision,
      };
      let measurement = metadata._defaultMeasurement;
      let report = await this.getReportToSubmit(taskId);
      if (report) {
        measurement = report.measurement;
        await this.#releasePendingReport(report);
      }

      sendPromises.push(
        DAPTelemetrySender.sendDAPMeasurement(task, measurement, {
          timeout,
          reason,
        })
      );
    }
    try {
      await Promise.all(sendPromises);
    } catch (e) {
      lazy.logConsole.error("Failed to send report: ", e);
    }
  }
}
