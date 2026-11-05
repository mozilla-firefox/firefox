/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DAPReportController, Task } from "./DAPReportController.sys.mjs";

let lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "DAPVisitCounter",
    maxLogLevelPref: "toolkit.telemetry.dap.logLevel",
  });
});
ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

export const DAPVisitCounter = new (class {
  counters = null;
  dapReportController = null;

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

    const placesTypes = ["page-visited", "history-cleared", "page-removed"];
    const placesListener = async events => {
      for (const event of events) {
        // Prioritizing data deletion.
        switch (event.type) {
          case "history-cleared":
          case "page-removed": {
            await this.dapReportController.deleteState();
            break;
          }
          case "page-visited": {
            // Even using the event.hidden flag there mayb be some double counting
            // here. It would have to be fixed in the Places API.
            if (!event.hidden) {
              for (const counter of this.counters) {
                for (const pattern of counter.patterns) {
                  if (pattern.matches(event.url)) {
                    lazy.logConsole.debug(`${pattern.pattern} matched!`);
                    await this.dapReportController.recordMeasurement(
                      counter.experiment.task_id,
                      counter.experiment.bucket
                    );
                  }
                }
              }
            }
          }
        }
      }
    };

    lazy.NimbusFeatures.dapTelemetry.onUpdate(async () => {
      if (this.counters !== null) {
        await this.dapReportController.cleanup(30 * 1000, "nimbus-update");
        this.counters = null;
        this.dapReportController = null;
      }

      // Clear registered calllbacks
      lazy.PlacesUtils.observers.removeListener(placesTypes, placesListener);

      // If we have an active Nimbus configuration, register this DAPVisitCounter.
      if (
        lazy.NimbusFeatures.dapTelemetry.getVariable("enabled") &&
        lazy.NimbusFeatures.dapTelemetry.getVariable("visitCountingEnabled")
      ) {
        this.initialize_counters();

        lazy.PlacesUtils.observers.addListener(placesTypes, placesListener);

        /*
          Intentionally not adding AsyncShutdown.appShutdownConfirmed.addBlocker.
          Attempting to send a report on shutdown causes a NetworkError which
          ultimately result in a lost report.  Since the pending report is
          persisted, it will be submitted on the next start.
         */

        let tasks = {};
        for (const counter of this.counters) {
          const task = new Task({
            taskId: counter.experiment.task_id,
            bits: 8,
            vdaf: "histogram",
            length: counter.experiment.task_veclen,
            timePrecision: 60,
            defaultMeasurement: 0,
          });
          tasks[counter.experiment.task_id] = task;
        }

        this.dapReportController = new DAPReportController({
          tasks,
          options: {
            windowDays: 7,
            submissionIntervalMins: 240,
          },
        });
        this.dapReportController.startTimedSubmission();
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

  show() {
    for (const counter of this.counters) {
      lazy.logConsole.info(`Experiment: ${counter.experiment.url}`);
    }
    return this.counters;
  }
})();
