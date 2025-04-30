/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { HPKEConfigManager } from "resource://gre/modules/HPKEConfigManager.sys.mjs";

let lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "DAPTelemetrySender",
    maxLogLevelPref: "toolkit.telemetry.dap.logLevel",
  });
});
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gTelemetryEnabled",
  "datareporting.healthreport.uploadEnabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gDapEndpoint",
  "toolkit.telemetry.dap.leader.url"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gLeaderHpke",
  "toolkit.telemetry.dap.leader.hpke"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gHelperHpke",
  "toolkit.telemetry.dap.helper.hpke"
);

/**
 * The purpose of this singleton is to handle sending of DAP telemetry data.
 * The current DAP draft standard is available here:
 * https://github.com/ietf-wg-ppm/draft-ietf-ppm-dap
 *
 * The specific purpose of this singleton is to make the necessary calls to fetch to do networking.
 */

export const DAPTelemetrySender = new (class {
  /**
   * @typedef { 'sum' | 'sumvec' | 'histogram' } VDAF
   */

  /**
   * Task configuration must match a configured task on the DAP server.
   *
   * @typedef {object} Task
   * @property {string} id - The task ID in urlsafe_base64 encoding.
   * @property {VDAF} vdaf - The VDAF used by the task.
   * @property {number} [bits] - The bit-width of integers in sum/sumvec measurements.
   * @property {number} [length] - The number of vector/histogram elements.
   * @property {number} time_precision - The rounding granularity in seconds
   *                                     that is applied to timestamps attached
   *                                     to the report.
   */

  async startup() {
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      lazy.logConsole.warn(
        "DAPTelemetrySender startup not possible due to shutdown."
      );
      return;
    }

    // Note that this can block until the ExperimentAPI is available.
    // This is fine as we depend on it. In case of a race with shutdown
    // it will reject, making the below getVariable calls return null.
    await lazy.NimbusFeatures.dapTelemetry.ready();

    if (
      lazy.NimbusFeatures.dapTelemetry.getVariable("enabled") &&
      lazy.NimbusFeatures.dapTelemetry.getVariable("task1Enabled")
    ) {
      let tasks = [];
      lazy.logConsole.debug("Task 1 is enabled.");
      let task1_id =
        lazy.NimbusFeatures.dapTelemetry.getVariable("task1TaskId");
      if (task1_id !== undefined && task1_id != "") {
        let task = {
          // this is testing task 1
          id: task1_id,
          vdaf: "sumvec",
          bits: 8,
          length: 20,
          time_precision: 300,
        };
        tasks.push(task);

        lazy.setTimeout(
          () => this.timedSendTestReports(tasks),
          this.timeout_value()
        );

        lazy.NimbusFeatures.dapTelemetry.onUpdate(async () => {
          if (typeof this.counters !== "undefined") {
            await this.sendTestReports(tasks, { reason: "nimbus-update" });
          }
        });
      }

      this._asyncShutdownBlocker = async () => {
        lazy.logConsole.debug(`Sending on shutdown.`);
        // Shorter timeout to prevent crashing due to blocking shutdown
        await this.sendTestReports(tasks, {
          timeout: 2_000,
          reason: "shutdown",
        });
      };

      lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
        "DAPTelemetrySender: sending data",
        this._asyncShutdownBlocker
      );
    }
  }

  async sendTestReports(tasks, options = {}) {
    for (let task of tasks) {
      let measurement;
      if (task.vdaf == "sum") {
        measurement = 3;
      } else if (task.vdaf == "sumvec") {
        measurement = new Array(20).fill(0);
        let r = Math.floor(Math.random() * 10);
        measurement[r] += 1;
        measurement[19] += 1;
      } else if (task.vdaf == "histogram") {
        measurement = Math.floor(Math.random() * 15);
      } else {
        throw new Error(`Unknown VDAF ${task.vdaf}`);
      }

      await this.sendDAPMeasurement(task, measurement, options);
    }
  }

  async timedSendTestReports(tasks) {
    lazy.logConsole.debug("Sending on timer.");
    await this.sendTestReports(tasks);
    lazy.setTimeout(
      () => this.timedSendTestReports(tasks),
      this.timeout_value()
    );
  }

  timeout_value() {
    const MINUTE = 60 * 1000;
    return MINUTE * (9 + Math.random() * 2); // 9 - 11 minutes
  }

  /**
   * Internal testing function to verify the DAP aggregator keys match current
   * values advertised by servers.
   */
  async checkHpkeKeys() {
    async function check_key(url, expected) {
      let response = await fetch(url + "/hpke_config");
      let body = await response.arrayBuffer();
      let actual = ChromeUtils.base64URLEncode(body, { pad: false });
      if (actual != expected) {
        throw new Error(`HPKE for ${url} does not match`);
      }
    }
    await Promise.allSettled([
      await check_key(
        Services.prefs.getStringPref("toolkit.telemetry.dap.leader.url"),
        Services.prefs.getStringPref("toolkit.telemetry.dap.leader.hpke")
      ),
      await check_key(
        Services.prefs.getStringPref("toolkit.telemetry.dap.helper.url"),
        Services.prefs.getStringPref("toolkit.telemetry.dap.helper.hpke")
      ),
    ]);
  }

  /**
   * Creates a DAP report for a specific task from a measurement and sends it.
   *
   * @param {Task} task
   *   Definition of the task for which the measurement was taken.
   * @param {number|Array<Number>} measurement
   *   The measured value for which a report is generated.
   * @param {object} options
   * @param {number} options.timeout
   *   The timeout for request in milliseconds. Defaults to 30s.
   * @param {string} options.reason
   *   A string to indicate the reason for triggering a submission. This is
   *   currently ignored and not recorded.
   * @param {string} options.ohttp_relay
   * @param {Uint8Array} options.ohttp_hpke
   *   If an OHTTP relay is specified, the reports are uploaded over OHTTP.
   */
  async sendDAPMeasurement(task, measurement, options = {}) {
    try {
      const controller = new AbortController();
      lazy.setTimeout(() => controller.abort(), options.timeout ?? 30_000);

      let keys = {
        leader_hpke: HPKEConfigManager.decodeKey(lazy.gLeaderHpke),
        helper_hpke: HPKEConfigManager.decodeKey(lazy.gHelperHpke),
      };

      let report = this.generateReport(task, measurement, keys);

      await this.sendReport(
        lazy.gDapEndpoint,
        task.id,
        report,
        controller.signal,
        options
      );
    } catch (e) {
      if (e.name === "AbortError") {
        lazy.logConsole.error("Aborted DAP report generation: ", e);
      } else {
        lazy.logConsole.error("DAP report generation failed: " + e);
      }

      throw e;
    }
  }

  /*
   * @typedef {object} AggregatorKeys
   * @property {Uint8Array} leader_hpke - The leader's DAP HPKE key.
   * @property {Uint8Array} helper_hpke - The helper's DAP HPKE key.
   */

  /**
   * Generates the encrypted DAP report.
   *
   * @param {Task} task
   *   Definition of the task for which the measurement was taken.
   * @param {number|Array<number>} measurement
   *   The measured value for which a report is generated.
   * @param {AggregatorKeys} keys
   *   The DAP encryption keys for each aggregator.
   *
   * @returns {ArrayBuffer} The generated binary report data.
   */
  generateReport(task, measurement, keys) {
    let task_id = new Uint8Array(
      ChromeUtils.base64URLDecode(task.id, { padding: "ignore" })
    );

    let reportOut = {};

    if (task.vdaf === "sum") {
      Services.DAPTelemetry.GetReportPrioSum(
        keys.leader_hpke,
        keys.helper_hpke,
        measurement,
        task_id,
        task.bits,
        task.time_precision,
        reportOut
      );
    } else if (task.vdaf === "sumvec") {
      if (measurement.length != task.length) {
        throw new Error(
          "Measurement vector length doesn't match task configuration"
        );
      }
      Services.DAPTelemetry.GetReportPrioSumVec(
        keys.leader_hpke,
        keys.helper_hpke,
        measurement,
        task_id,
        task.bits,
        task.time_precision,
        reportOut
      );
    } else if (task.vdaf === "histogram") {
      Services.DAPTelemetry.GetReportPrioHistogram(
        keys.leader_hpke,
        keys.helper_hpke,
        measurement,
        task_id,
        task.length,
        task.time_precision,
        reportOut
      );
    } else {
      throw new Error(
        `Unknown measurement type for task ${task.id}: ${task.vdaf} ${task.bits}`
      );
    }

    return new Uint8Array(reportOut.value).buffer;
  }

  /**
   * Sends a report to the leader.
   *
   * @param {string} leader_endpoint
   *   The URL for the leader.
   * @param {string} task_id
   *   Base64 encoded task_id as it appears in the upload path.
   * @param {ArrayBuffer} report
   *   Raw bytes of the TLS encoded report.
   * @param {AbortSignal} abortSignal
   *   Can be used to cancel network requests. Does not cancel computation.
   * @param {object} options
   * @param {string} options.ohttp_relay
   * @param {Uint8Array} options.ohttp_hpke
   *   If an OHTTP relay is specified, the reports are uploaded over OHTTP. In
   *   this case, the OHTTP and DAP keys must be provided and this code will not
   *   attempt to fetch them.
   *
   * @returns Promise
   * @resolves {undefined} Once the attempt to send the report completes, whether or not it was successful.
   */
  async sendReport(leader_endpoint, task_id, report, abortSignal, options) {
    // If telemetry disabled, don't upload DAP reports either.
    if (!lazy.gTelemetryEnabled) {
      return;
    }

    const upload_path = leader_endpoint + "/tasks/" + task_id + "/reports";
    try {
      let requestOptions = {
        method: "PUT",
        headers: { "Content-Type": "application/dap-report" },
        body: report,
        signal: abortSignal,
      };
      let response;
      if (options.ohttp_relay) {
        response = await lazy.ObliviousHTTP.ohttpRequest(
          options.ohttp_relay,
          options.ohttp_hpke,
          upload_path,
          requestOptions
        );
      } else {
        response = await fetch(upload_path, requestOptions);
      }

      if (response.status != 200) {
        const content_type = response.headers.get("content-type");
        if (content_type && content_type === "application/json") {
          // A JSON error from the DAP server.
          let error = await response.json();
          throw new Error(
            `Sending failed. HTTP response: ${response.status} ${response.statusText}. Error: ${error.type} ${error.title}`
          );
        } else {
          // A different error, e.g. from a load-balancer.
          let error = await response.text();
          throw new Error(
            `Sending failed. HTTP response: ${response.status} ${response.statusText}. Error: ${error}`
          );
        }
      } else {
        lazy.logConsole.debug("DAP report sent");
      }
    } catch (err) {
      if (err.name === "AbortError") {
        lazy.logConsole.error("Aborted DAP report sending: ", err);
      } else {
        lazy.logConsole.error("Failed to send report: ", err);
      }

      throw err;
    }
  }
})();
