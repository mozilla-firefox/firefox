/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { DAPSender } from "resource://gre/modules/DAPSender.sys.mjs";

let lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "DAPTelemetrySender",
    maxLogLevelPref: "toolkit.telemetry.dap.logLevel",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gTelemetryEnabled",
  "datareporting.healthreport.uploadEnabled",
  false
);

/**
 *
 * This class wraps DAPSender and adds telemetry-specific logic, ensuring that
 * DAP reports are only sent when telemetry is enabled.
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
          () => DAPSender.timedSendTestReports(tasks),
          DAPSender.timeout_value()
        );

        lazy.NimbusFeatures.dapTelemetry.onUpdate(async () => {
          if (typeof this.counters !== "undefined") {
            await DAPSender.sendTestReports(tasks, { reason: "nimbus-update" });
          }
        });
      }

      this._asyncShutdownBlocker = async () => {
        lazy.logConsole.debug(`Sending on shutdown.`);
        // Shorter timeout to prevent crashing due to blocking shutdown
        await DAPSender.sendTestReports(tasks, {
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

  /**
   * Creates a DAP report for a specific task from a measurement and sends it if telemetry is enabled.
   *
   * @param {Task} task
   *   Definition of the task for which the measurement was taken.
   * @param {number|Array<number>} measurement
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
    if (!lazy.gTelemetryEnabled) {
      return;
    }

    await DAPSender.sendDAPMeasurement(task, measurement, options);
  }
})();
