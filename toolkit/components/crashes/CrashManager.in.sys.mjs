/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { setTimeout } from "resource://gre/modules/Timer.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  CrashServiceUtils: "resource://gre/modules/CrashService.sys.mjs",
  Log: "resource://gre/modules/Log.sys.mjs",
  Subprocess: "resource://gre/modules/Subprocess.sys.mjs",
});

/**
 * How long to wait after application startup before crash event files are
 * automatically aggregated.
 *
 * We defer aggregation for performance reasons, as we don't want too many
 * services competing for I/O immediately after startup.
 */
const AGGREGATE_STARTUP_DELAY_MS = 57000;

const MILLISECONDS_IN_DAY = 24 * 60 * 60 * 1000;

// Converts Date to days since UNIX epoch.
// The implementation does not account for leap seconds.
export function dateToDays(date) {
  return Math.floor(date.getTime() / MILLISECONDS_IN_DAY);
}

// sendGleanPing will sequence calls so that we only have one crash reporter
// spawned at a time.
let lastSendPing = Promise.resolve();
function sendGleanPing(reason, annotations) {
  const doSendPing = async () => {
    const uAppDataPath = Services.dirsvc.get("UAppData", Ci.nsIFile).path;
    const crashDataPath = PathUtils.join(uAppDataPath, "Crash Reports");

    const process = await lazy.Subprocess.call({
      command: lazy.CrashServiceUtils.getCrashReporterPath().path,
      arguments: ["--send-ping", crashDataPath, reason],
      stderr: "stdout",
    });

    await process.stdin.write(JSON.stringify(annotations));
    await process.stdin.close();
    const { exitCode } = await process.wait();

    if (exitCode !== 0) {
      let output = "";
      let s;
      while ((s = await process.stdout.readString())) {
        output += s;
      }
      console.error(`failed to send ping: ${output}`);
    }
    // Flush stdout so any pending data is consumed. process.stdout.close()
    // should have the same effect, but it doesn't seem to behave well (reporting
    // that the file is closed).
    while ((await process.stdout.read()).byteLength) {
      // We don't need to do anything, just wait for an empty read
    }
  };

  // `Promise.prototype.finally()` will resolve to the prior Promise state, so
  // we use `.then()`, since we want to sequence calls but return the error
  // from this `doSendPing()`.
  lastSendPing = lastSendPing.then(doSendPing, doSendPing);
  lazy.AsyncShutdown.sendTelemetry.addBlocker(
    "CrashManager: submitting Glean crash ping(s)",
    lastSendPing
  );
  return (lastSendPing = lastSendPing.finally(() =>
    lazy.AsyncShutdown.sendTelemetry.removeBlocker(lastSendPing)
  ));
}

/*
 * Clean up pending Glean pings.
 *
 * We don't care about synchronizing with lastSendPing here because this will
 * only occur once per application session, and the crashreporter already
 * handles its own exclusive locking.
 *
 * Returns whether the crashreporter exited without error.
 */
async function cleanupPings() {
  const uAppDataPath = Services.dirsvc.get("UAppData", Ci.nsIFile).path;
  let profPath;
  try {
    profPath = Services.dirsvc.get("ProfD", Ci.nsIFile).path;
  } catch (e) {
    // If there is no profile dir, leave the path undefined.
  }
  const crashDataPath = PathUtils.join(uAppDataPath, "Crash Reports");

  const args = ["--ping-cleanup", crashDataPath];
  if (profPath !== undefined) {
    args.push(profPath);
  }

  const process = await lazy.Subprocess.call({
    command: lazy.CrashServiceUtils.getCrashReporterPath().path,
    arguments: args,
  });

  const blocker = () => process.kill();

  lazy.AsyncShutdown.profileBeforeChange.addBlocker(
    "CrashManager: killing ping cleanup process",
    blocker
  );

  await process.stdin.close();
  // This is best-effort: we don't care about failure.
  const { exitCode } = await process.wait();
  lazy.AsyncShutdown.profileBeforeChange.removeBlocker(blocker);
  while ((await process.stdout.read()).byteLength) {
    // Flush stdout to avoid leaking buffered data.
  }

  return exitCode === 0;
}

/**
 * A gateway to crash-related data.
 *
 * This type is generic and can be instantiated any number of times.
 * However, most applications will typically only have one instance
 * instantiated and that instance will point to profile and user appdata
 * directories.
 *
 * Instances are created by passing an object with properties.
 * Recognized properties are:
 *
 *   pendingDumpsDir (string) (required)
 *     Where dump files that haven't been uploaded are located.
 *
 *   submittedDumpsDir (string) (required)
 *     Where records of uploaded dumps are located.
 *
 *   eventsDirs (array)
 *     Directories (defined as strings) where events files are written. This
 *     instance will collects events from files in the directories specified.
 *
 *   storeDir (string)
 *     Directory we will use for our data store. This instance will write
 *     data files into the directory specified.
 */
export var CrashManager = function (options) {
  for (let k in options) {
    let value = options[k];

    switch (k) {
      case "pendingDumpsDir":
      case "submittedDumpsDir":
      case "eventsDirs":
      case "storeDir": {
        let key = "_" + k;
        delete this[key];
        Object.defineProperty(this, key, { value });
        break;
      }

      default:
        throw new Error("Unknown property in options: " + k);
    }
  }

  // Promise for in-progress aggregation operation. We store it on the
  // object so it can be returned for in-progress operations.
  this._aggregatePromise = null;

  // Map of crash ID / promise tuples used to track adding new crashes.
  this._crashPromises = new Map();

  // Promise for the crash pings used only for testing.
  this._gleanPingPromise = null;

  // Tests may disable the Glean ping to avoid unnecessary overhead of invoking
  // the crash reporter client.
  this._disableGleanPing = false;

  // The CrashStore currently attached to this object.
  this._store = null;

  // A Task to retrieve the store. This is needed to avoid races when
  // _getStore() is called multiple times in a short interval.
  this._getStoreTask = null;

  // The timer controlling the expiration of the CrashStore instance.
  this._storeTimer = null;

  // This is a semaphore that prevents the store from being freed by our
  // timer-based resource freeing mechanism.
  this._storeProtectedCount = 0;
};

CrashManager.prototype = Object.freeze({
  // gen_CrashManager.py will input the proper process map informations.
  /* SUBST: CRASH_MANAGER_PROCESS_MAP */

  // A real crash.
  CRASH_TYPE_CRASH: "crash",

  // A hang.
  CRASH_TYPE_HANG: "hang",

  // Submission result values.
  SUBMISSION_RESULT_OK: "ok",
  SUBMISSION_RESULT_FAILED: "failed",

  DUMP_REGEX:
    /^([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\.dmp$/i,
  SUBMITTED_REGEX:
    /^bp-(?:hr-)?([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\.txt$/i,
  ALL_REGEX: /^(.*)$/,

  // How long the store object should persist in memory before being
  // automatically garbage collected.
  STORE_EXPIRATION_MS: 60 * 1000,

  // Number of days after which a crash with no activity will get purged.
  PURGE_OLDER_THAN_DAYS: 180,

  // The following are return codes for individual event file processing.
  // File processed OK.
  EVENT_FILE_SUCCESS: "ok",
  // The event appears to be malformed.
  EVENT_FILE_ERROR_MALFORMED: "malformed",
  // The event is obsolete.
  EVENT_FILE_ERROR_OBSOLETE: "obsolete",
  // The type of event is unknown.
  EVENT_FILE_ERROR_UNKNOWN_EVENT: "unknown-event",

  _lazyGetDir(field, path, leaf) {
    delete this[field];
    let value = PathUtils.join(path, leaf);
    Object.defineProperty(this, field, { value });
    return value;
  },

  get _crDir() {
    return this._lazyGetDir(
      "_crDir",
      Services.dirsvc.get("UAppData", Ci.nsIFile).path,
      "Crash Reports"
    );
  },

  get _storeDir() {
    return this._lazyGetDir(
      "_storeDir",
      Services.dirsvc.get("ProfD", Ci.nsIFile).path,
      "crashes"
    );
  },

  get _pendingDumpsDir() {
    return this._lazyGetDir("_pendingDumpsDir", this._crDir, "pending");
  },

  get _submittedDumpsDir() {
    return this._lazyGetDir("_submittedDumpsDir", this._crDir, "submitted");
  },

  get _eventsDirs() {
    delete this._eventsDirs;
    let value = [
      PathUtils.join(this._crDir, "events"),
      PathUtils.join(this._storeDir, "events"),
    ];
    Object.defineProperty(this, "_eventsDirs", { value });
    return value;
  },

  /**
   * Obtain a list of all dumps pending upload.
   *
   * The returned value is a promise that resolves to an array of objects
   * on success. Each element in the array has the following properties:
   *
   *   id (string)
   *      The ID of the crash (a UUID).
   *
   *   path (string)
   *      The filename of the crash (<UUID.dmp>)
   *
   *   date (Date)
   *      When this dump was created
   *
   * The returned arry is sorted by the modified time of the file backing
   * the entry, oldest to newest.
   *
   * @return Promise<Array>
   */
  pendingDumps() {
    return this._getDirectoryEntries(this._pendingDumpsDir, this.DUMP_REGEX);
  },

  /**
   * Obtain a list of all dump files corresponding to submitted crashes.
   *
   * The returned value is a promise that resolves to an Array of
   * objects. Each object has the following properties:
   *
   *   path (string)
   *     The path of the file this entry comes from.
   *
   *   id (string)
   *     The crash UUID.
   *
   *   date (Date)
   *     The (estimated) date this crash was submitted.
   *
   * The returned array is sorted by the modified time of the file backing
   * the entry, oldest to newest.
   *
   * @return Promise<Array>
   */
  submittedDumps() {
    return this._getDirectoryEntries(
      this._submittedDumpsDir,
      this.SUBMITTED_REGEX
    );
  },

  /**
   * Aggregates "loose" events files into the unified "database."
   *
   * This function should be called periodically to collect metadata from
   * all events files into the central data store maintained by this manager.
   *
   * Once events have been stored in the backing store the corresponding
   * source files are deleted.
   *
   * Only one aggregation operation is allowed to occur at a time. If this
   * is called when an existing aggregation is in progress, the promise for
   * the original call will be returned.
   *
   * @return promise<int> The number of event files that were examined.
   */
  aggregateEventsFiles() {
    if (this._aggregatePromise) {
      return this._aggregatePromise;
    }

    return (this._aggregatePromise = (async () => {
      if (this._aggregatePromise) {
        return this._aggregatePromise;
      }

      try {
        let unprocessedFiles = await this._getUnprocessedEventsFiles();

        let deletePaths = [];
        let needsSave = false;

        this._storeProtectedCount++;
        for (let entry of unprocessedFiles) {
          try {
            let result = await this._processEventFile(entry);

            switch (result) {
              case this.EVENT_FILE_SUCCESS:
                needsSave = true;
              // Fall through.

              case this.EVENT_FILE_ERROR_MALFORMED:
              case this.EVENT_FILE_ERROR_OBSOLETE:
                deletePaths.push(entry.path);
                break;

              case this.EVENT_FILE_ERROR_UNKNOWN_EVENT:
                break;

              default:
                console.error(
                  "Unhandled crash event file return code. Please " +
                    "file a bug: ",
                  result
                );
            }
          } catch (ex) {
            if (DOMException.isInstance(ex)) {
              this._log.warn("I/O error reading " + entry.path, ex);
            } else {
              // We should never encounter an exception. This likely represents
              // a coding error because all errors should be detected and
              // converted to return codes.
              //
              // If we get here, report the error and delete the source file
              // so we don't see it again.
              console.error(
                "Exception when processing crash event file: " +
                  lazy.Log.exceptionStr(ex)
              );
              deletePaths.push(entry.path);
            }
          }
        }

        if (needsSave) {
          let store = await this._getStore();

          if (store) {
            await store.save();
          }
        }

        for (let path of deletePaths) {
          try {
            await IOUtils.remove(path);
          } catch (ex) {
            this._log.warn("Error removing event file (" + path + ")", ex);
          }
        }

        return unprocessedFiles.length;
      } finally {
        this._aggregatePromise = false;
        this._storeProtectedCount--;
      }
    })());
  },

  /**
   * Prune old crash data.
   *
   * @param date
   *        (Date) The cutoff point for pruning. Crashes without data newer
   *        than this will be pruned.
   */
  pruneOldCrashes(date) {
    return (async () => {
      let store = await this._getStore();

      if (store) {
        store.pruneOldCrashes(date);
        await store.save();
      }
    })();
  },

  async sendUnsubmittedPings() {
    let store = await this._getStore();
    if (!store) {
      return;
    }

    const crashes = store.crashesWithoutPingSubmissions();
    for (const crash of crashes) {
      this._sendCrashPing(
        store,
        crash.id,
        "crash",
        crash.processType,
        crash.crashDate,
        crash.metadata
      );
    }
    await store.save();
  },

  /**
   * Run tasks that should be periodically performed.
   */
  runMaintenanceTasks() {
    return (async () => {
      await this.aggregateEventsFiles();
      await this.sendUnsubmittedPings();

      let offset = this.PURGE_OLDER_THAN_DAYS * MILLISECONDS_IN_DAY;
      await this.pruneOldCrashes(new Date(Date.now() - offset));

      if (AppConstants.platform !== "android" && !this._disableGleanPing) {
        this._cleanupPingsResult = await cleanupPings().catch(error => {
          // The pipes can race (especially if the program exits quickly) and
          // throw File closed. Don't treat it as an error.
          if (error.message === "File closed") {
            return true;
          }
          console.error(`failed to cleanup Glean crash pings: ${error}`);
          return false;
        });
      }
    })();
  },

  /**
   * Schedule maintenance tasks for some point in the future.
   *
   * @param delay
   *        (integer) Delay in milliseconds when maintenance should occur.
   */
  scheduleMaintenance(delay) {
    let deferred = Promise.withResolvers();

    setTimeout(() => {
      this.runMaintenanceTasks().then(deferred.resolve, deferred.reject);
    }, delay);

    return deferred.promise;
  },

  /**
   * Record the occurrence of a crash.
   *
   * This method skips event files altogether and writes directly and
   * immediately to the manager's data store.
   *
   * @param processType (string) One of the PROCESS_TYPE constants.
   * @param crashType (string) One of the CRASH_TYPE constants.
   * @param id (string) Crash ID. Likely a UUID.
   * @param date (Date) When the crash occurred.
   * @param metadata (dictionary) Crash metadata, may be empty.
   *
   * @return promise<null> Resolved when the store has been saved.
   */
  addCrash(processType, crashType, id, date, metadata) {
    let promise = (async () => {
      if (!this.isValidProcessType(processType)) {
        console.error(
          "Unhandled process type. Please file a bug: '",
          processType,
          "'. Ignore in the context of " +
            "test_crash_manager.js:test_addCrashWrong()."
        );
        return;
      }

      let store = await this._getStore();
      let needSave = false;

      if (store) {
        needSave = store.addCrash(processType, crashType, id, date, metadata);
      }

      if (this.isPingAllowed(processType)) {
        this._sendCrashPing(store, id, "crash", processType, date, metadata);
      }

      if (needSave) {
        await store.save();
      }

      let deferred = this._crashPromises.get(id);

      if (deferred) {
        this._crashPromises.delete(id);
        deferred.resolve();
      }
    })();

    return promise;
  },

  /**
   * Check that the processType parameter is a valid one:
   *  - it is a string
   *  - it is listed in this.processTypes
   *
   * @param processType (string) Process type to evaluate
   *
   * @return boolean True or false depending whether it is a legit one
   */
  isValidProcessType(processType) {
    if (typeof processType !== "string") {
      return false;
    }

    for (const pt of Object.values(this.processTypes)) {
      if (pt === processType) {
        return true;
      }
    }

    return false;
  },

  /**
   * Check that processType is allowed to send a ping
   *
   * @param processType (string) Process type to check for
   *
   * @return boolean True or False depending on whether ping is allowed
   */
  isPingAllowed(processType) {
    // gen_CrashManager.py will input the proper process pings informations.

    let processPings = {
      /* SUBST: CRASH_MANAGER_PROCESS_PINGS */
    };

    // Should not even reach this because of isValidProcessType() but just in
    // case we try to be cautious
    if (!(processType in processPings)) {
      return false;
    }

    return processPings[processType];
  },

  /**
   * Returns a promise that is resolved only the crash with the specified id
   * has been fully recorded.
   *
   * @param id (string) Crash ID. Likely a UUID.
   *
   * @return promise<null> Resolved when the crash is present.
   */
  async ensureCrashIsPresent(id) {
    let store = await this._getStore();
    if (!store) {
      return Promise.reject();
    }

    let crash = store.getCrash(id);

    if (crash) {
      return Promise.resolve();
    }

    let deferred = Promise.withResolvers();

    this._crashPromises.set(id, deferred);
    return deferred.promise;
  },

  /**
   * Record the remote ID for a crash.
   *
   * @param crashID (string) Crash ID. Likely a UUID.
   * @param remoteID (Date) Server/Breakpad ID.
   *
   * @return boolean True if the remote ID was recorded.
   */
  async setRemoteCrashID(crashID, remoteID) {
    let store = await this._getStore();
    if (store && store.setRemoteCrashID(crashID, remoteID)) {
      await store.save();
    }
  },

  /**
   * Generate a submission ID for use with addSubmission{Attempt,Result}.
   */
  generateSubmissionID() {
    return "sub-" + Services.uuid.generateUUID().toString().slice(1, -1);
  },

  /**
   * Record the occurrence of a submission attempt for a crash.
   *
   * @param crashID (string) Crash ID. Likely a UUID.
   * @param submissionID (string) Submission ID. Likely a UUID.
   * @param date (Date) When the attempt occurred.
   *
   * @return boolean True if the attempt was recorded and false if not.
   */
  async addSubmissionAttempt(crashID, submissionID, date) {
    let store = await this._getStore();
    if (store && store.addSubmissionAttempt(crashID, submissionID, date)) {
      await store.save();
    }
  },

  /**
   * Record the occurrence of a submission result for a crash.
   *
   * @param crashID (string) Crash ID. Likely a UUID.
   * @param submissionID (string) Submission ID. Likely a UUID.
   * @param date (Date) When the submission result was obtained.
   * @param result (string) One of the SUBMISSION_RESULT constants.
   *
   * @return boolean True if the result was recorded and false if not.
   */
  async addSubmissionResult(crashID, submissionID, date, result) {
    let store = await this._getStore();
    if (
      store &&
      store.addSubmissionResult(crashID, submissionID, date, result)
    ) {
      await store.save();
    }
  },

  /**
   * Set the classification of a crash.
   *
   * @param crashID (string) Crash ID. Likely a UUID.
   * @param classifications (array) Crash classifications.
   *
   * @return boolean True if the data was recorded and false if not.
   */
  async setCrashClassifications(crashID, classifications) {
    let store = await this._getStore();
    if (store && store.setCrashClassifications(crashID, classifications)) {
      await store.save();
    }
  },

  /**
   * Obtain the paths of all unprocessed events files.
   *
   * The promise-resolved array is sorted by file mtime, oldest to newest.
   */
  _getUnprocessedEventsFiles() {
    return (async () => {
      try {
        let entries = [];

        for (let dir of this._eventsDirs) {
          for (let e of await this._getDirectoryEntries(dir, this.ALL_REGEX)) {
            entries.push(e);
          }
        }

        entries.sort((a, b) => {
          return a.date - b.date;
        });

        return entries;
      } catch (e) {
        console.error(e);
        return [];
      }
    })();
  },

  // See docs/crash-events.rst for the file format specification.
  _processEventFile(entry) {
    return (async () => {
      let data = await IOUtils.read(entry.path);
      let store = await this._getStore();

      if (!store) {
        return this.EVENT_FILE_ERROR_UNKNOWN_EVENT;
      }

      let decoder = new TextDecoder();
      data = decoder.decode(data);

      let type, time;
      let start = 0;
      for (let i = 0; i < 2; i++) {
        let index = data.indexOf("\n", start);
        if (index == -1) {
          return this.EVENT_FILE_ERROR_MALFORMED;
        }

        let sub = data.substring(start, index);
        switch (i) {
          case 0:
            type = sub;
            break;
          case 1:
            time = sub;
            try {
              time = parseInt(time, 10);
            } catch (ex) {
              return this.EVENT_FILE_ERROR_MALFORMED;
            }
        }

        start = index + 1;
      }
      let date = new Date(time * 1000);
      let payload = data.substring(start);

      return this._handleEventFilePayload(store, entry, type, date, payload);
    })();
  },

  /**
   * Send a crash ping.
   *
   * @param {object} store - The crash store.
   * @param {string} crashId - the crash identifier
   * @param {string} reason - the reason for the crash ping, one of: "crash", "event_found"
   * @param {string} type - the process type (from {@link processTypes})
   * @param {DateTime} date - the time of the crash (or the closest time after it)
   * @param {object} metadata - Telemetry crash metadata
   */
  _sendCrashPing(store, crashId, reason, type, date, metadata = {}) {
    // If we're shutting down, do nothing (we'll send the ping later, see
    // `sendUnsubmittedPings`).
    if (Services.startup.shuttingDown) {
      return;
    }

    // Glean crash pings should not be sent on Android: they are handled
    // separately in lib-crash for Fenix (and potentially other GeckoView
    // users).
    this._gleanPingPromise = null;
    const telemetryEnabled = Services.prefs.getBoolPref(
      "datareporting.healthreport.uploadEnabled",
      true
    );
    if (
      telemetryEnabled &&
      AppConstants.platform !== "android" &&
      !this._disableGleanPing
    ) {
      const pingMeta = Cu.cloneInto(metadata, {});
      // Delete unused fields from legacy telemetry
      delete pingMeta.TelemetryEnvironment;
      delete pingMeta.TelemetrySessionId;
      pingMeta.CrashTime ??= Math.floor(date.getTime() / 1000).toString();
      pingMeta.ProcessType ??= type;

      this._gleanPingPromise = sendGleanPing(reason, pingMeta).catch(error => {
        console.error(`failed to send Glean crash ping: ${error}`);
        throw error;
      });
    }
    if (store) {
      // Consider the ping submitted, which means that the crashreporter client
      // is passed the ping data (if applicable). This _doesn't_ mean the ping
      // was received by the telemetry server. We rely on the crashreporter
      // client's Glean instance to handle reliable delivery.
      store.setPingSubmitted(crashId);
    }
  },

  _handleEventFilePayload(store, entry, type, date, payload) {
    // The payload types and formats are documented in docs/crash-events.rst.
    // Do not change the format of an existing type. Instead, invent a new
    // type.
    // DO NOT ADD NEW TYPES WITHOUT DOCUMENTING!
    let lines = payload.split("\n");

    switch (type) {
      case "crash.main.1":
      case "crash.main.2":
        return this.EVENT_FILE_ERROR_OBSOLETE;

      case "crash.main.3": {
        let crashID = lines[0];
        let metadata = JSON.parse(lines[1]);
        store.addCrash(
          this.processTypes[Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT],
          this.CRASH_TYPE_CRASH,
          crashID,
          date,
          metadata
        );

        this._sendCrashPing(
          store,
          crashID,
          "event_found",
          this.processTypes[Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT],
          date,
          metadata
        );

        break;
      }

      case "crash.submission.1":
        if (lines.length == 3) {
          let [crashID, result, remoteID] = lines;
          store.addCrash(
            this.processTypes[Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT],
            this.CRASH_TYPE_CRASH,
            crashID,
            date
          );

          // We consider the crash to have already had a ping sent by the
          // crashreporter client.
          store.setPingSubmitted(crashID);

          let submissionID = this.generateSubmissionID();
          let succeeded = result === "true";
          store.addSubmissionAttempt(crashID, submissionID, date);
          store.addSubmissionResult(
            crashID,
            submissionID,
            date,
            succeeded
              ? this.SUBMISSION_RESULT_OK
              : this.SUBMISSION_RESULT_FAILED
          );
          if (succeeded) {
            store.setRemoteCrashID(crashID, remoteID);
          }
        } else {
          return this.EVENT_FILE_ERROR_MALFORMED;
        }
        break;

      default:
        return this.EVENT_FILE_ERROR_UNKNOWN_EVENT;
    }

    return this.EVENT_FILE_SUCCESS;
  },

  /**
   * The resolved promise is an array of objects with the properties:
   *
   *   path -- String filename
   *   id -- regexp.match()[1] (likely the crash ID)
   *   date -- Date mtime of the file
   */
  _getDirectoryEntries(path, re) {
    return (async function () {
      let entries = [];

      try {
        let children = await IOUtils.getChildren(path);

        for (const entry of children) {
          let stat = await IOUtils.stat(entry);
          if (stat.type == "directory") {
            continue;
          }

          let filename = PathUtils.filename(entry);
          let match = re.exec(filename);
          if (!match) {
            continue;
          }
          entries.push({
            path: entry,
            id: match[1],
            date: stat.lastModified,
          });
        }

        entries.sort((a, b) => {
          return a.date - b.date;
        });
      } catch (ex) {
        // Missing events folders are allowed
        console.error(ex);
      }

      return entries;
    })();
  },

  _getStore() {
    if (this._getStoreTask) {
      return this._getStoreTask;
    }

    return (this._getStoreTask = (async () => {
      try {
        if (!this._store) {
          await IOUtils.makeDirectory(this._storeDir, {
            permissions: 0o700,
          });

          let store = new CrashStore(this._storeDir);
          await store.load();

          this._store = store;
          this._storeTimer = Cc["@mozilla.org/timer;1"].createInstance(
            Ci.nsITimer
          );
        }

        // The application can go long periods without interacting with the
        // store. Since the store takes up resources, we automatically "free"
        // the store after inactivity so resources can be returned to the
        // system. We do this via a timer and a mechanism that tracks when the
        // store is being accessed.
        this._storeTimer.cancel();

        // This callback frees resources from the store unless the store
        // is protected from freeing by some other process.
        let timerCB = () => {
          if (this._storeProtectedCount) {
            this._storeTimer.initWithCallback(
              timerCB,
              this.STORE_EXPIRATION_MS,
              this._storeTimer.TYPE_ONE_SHOT
            );
            return;
          }

          // We kill the reference that we hold. GC will kill it later. If
          // someone else holds a reference, that will prevent GC until that
          // reference is gone.
          this._store = null;
          this._storeTimer = null;
        };

        this._storeTimer.initWithCallback(
          timerCB,
          this.STORE_EXPIRATION_MS,
          this._storeTimer.TYPE_ONE_SHOT
        );

        return this._store;
      } finally {
        this._getStoreTask = null;
      }
    })());
  },

  /**
   * Obtain information about all known crashes.
   *
   * Returns an array of CrashRecord instances. Instances are read-only.
   */
  getCrashes() {
    return (async () => {
      let store = await this._getStore();

      if (!store) {
        return [];
      }

      return store.crashes;
    })();
  },
});

var gCrashManager;

/**
 * Interface to storage of crash data.
 *
 * This type handles storage of crash metadata. It exists as a separate type
 * from the crash manager for performance reasons: since all crash metadata
 * needs to be loaded into memory for access, we wish to easily dispose of all
 * associated memory when this data is no longer needed. Having an isolated
 * object whose references can easily be lost faciliates that simple disposal.
 *
 * When metadata is updated, the caller must explicitly persist the changes
 * to disk. This prevents excessive I/O during updates.
 *
 * The store has a mechanism for ensuring it doesn't grow too large. A ceiling
 * is placed on the number of daily events that can occur for events that can
 * occur with relatively high frequency. If we've reached
 * the high water mark and new data arrives, it's silently dropped.
 * However, the count of actual events is always preserved. This allows
 * us to report on the severity of problems beyond the storage threshold.
 *
 * Main process crashes are excluded from limits because they are both
 * important and should be rare.
 *
 * @param storeDir (string)
 *        Directory the store should be located in.
 */
export function CrashStore(storeDir) {
  this._storeDir = storeDir;

  this._storePath = PathUtils.join(storeDir, "store.json.mozlz4");

  // Holds the read data from disk.
  this._data = null;

  // Maps days since UNIX epoch to a Map of event types to counts.
  // This data structure is populated when the JSON file is loaded
  // and is also updated when new events are added.
  this._countsByDay = new Map();
}

CrashStore.prototype = Object.freeze({
  // Maximum number of events to store per day. This establishes a
  // ceiling on the per-type/per-day records that will be stored.
  HIGH_WATER_DAILY_THRESHOLD: 500,

  /**
   * Reset all data.
   */
  reset() {
    this._data = {
      v: 1,
      crashes: new Map(),
      corruptDate: null,
    };
    this._countsByDay = new Map();
  },

  /**
   * Load data from disk.
   *
   * @return Promise
   */
  load() {
    return (async () => {
      // Loading replaces data.
      this.reset();

      try {
        let decoder = new TextDecoder();
        let data = await IOUtils.read(this._storePath, { decompress: true });
        data = JSON.parse(decoder.decode(data));

        if (data.corruptDate) {
          this._data.corruptDate = new Date(data.corruptDate);
        }

        // actualCounts is used to validate that the derived counts by
        // days stored in the payload matches up to actual data.
        let actualCounts = new Map();

        // In the past, submissions were stored as separate crash records
        // with an id of e.g. "someID-submission". If we find IDs ending
        // with "-submission", we will need to convert the data to be stored
        // as actual submissions.
        //
        // The old way of storing submissions was used from FF33 - FF34. We
        // drop this old data on the floor.
        for (let id in data.crashes) {
          if (id.endsWith("-submission")) {
            continue;
          }

          let crash = data.crashes[id];
          let denormalized = this._denormalize(crash);

          denormalized.submissions = new Map();
          if (crash.submissions) {
            for (let submissionID in crash.submissions) {
              let submission = crash.submissions[submissionID];
              denormalized.submissions.set(
                submissionID,
                this._denormalize(submission)
              );
            }
          }

          this._data.crashes.set(id, denormalized);

          let key =
            dateToDays(denormalized.crashDate) + "-" + denormalized.type;
          actualCounts.set(key, (actualCounts.get(key) || 0) + 1);

          // If we have an OOM size, count the crash as an OOM in addition to
          // being a main process crash.
          if (
            denormalized.metadata &&
            denormalized.metadata.OOMAllocationSize
          ) {
            let oomKey = key + "-oom";
            actualCounts.set(oomKey, (actualCounts.get(oomKey) || 0) + 1);
          }
        }

        // The validation in this loop is arguably not necessary. We perform
        // it as a defense against unknown bugs.
        for (let dayKey in data.countsByDay) {
          let day = parseInt(dayKey, 10);
          for (let type in data.countsByDay[day]) {
            this._ensureCountsForDay(day);

            let count = data.countsByDay[day][type];
            let key = day + "-" + type;

            // If the payload says we have data for a given day but we
            // don't, the payload is wrong. Ignore it.
            if (!actualCounts.has(key)) {
              continue;
            }

            // If we encountered more data in the payload than what the
            // data structure says, use the proper value.
            count = Math.max(count, actualCounts.get(key));

            this._countsByDay.get(day).set(type, count);
          }
        }
      } catch (ex) {
        // Missing files (first use) are allowed.
        if (!DOMException.isInstance(ex) || ex.name != "NotFoundError") {
          // If we can't load for any reason, mark a corrupt date in the instance
          // and swallow the error.
          //
          // The marking of a corrupted file is intentionally not persisted to
          // disk yet. Instead, we wait until the next save(). This is to give
          // non-permanent failures the opportunity to recover on their own.
          this._data.corruptDate = new Date();
        }
      }
    })();
  },

  /**
   * Save data to disk.
   *
   * @return Promise<null>
   */
  save() {
    return (async () => {
      if (!this._data) {
        return;
      }

      let normalized = {
        // The version should be incremented whenever the format
        // changes.
        v: 1,
        // Maps crash IDs to objects defining the crash.
        crashes: {},
        // Maps days since UNIX epoch to objects mapping event types to
        // counts. This is a mirror of this._countsByDay. e.g.
        // {
        //    15000: {
        //        "main-crash": 2,
        //        "plugin-crash": 1
        //    }
        // }
        countsByDay: {},

        // When the store was last corrupted.
        corruptDate: null,
      };

      if (this._data.corruptDate) {
        normalized.corruptDate = this._data.corruptDate.getTime();
      }

      for (let [id, crash] of this._data.crashes) {
        let c = this._normalize(crash);

        c.submissions = {};
        for (let [submissionID, submission] of crash.submissions) {
          c.submissions[submissionID] = this._normalize(submission);
        }

        normalized.crashes[id] = c;
      }

      for (let [day, m] of this._countsByDay) {
        normalized.countsByDay[day] = {};
        for (let [type, count] of m) {
          normalized.countsByDay[day][type] = count;
        }
      }

      let encoder = new TextEncoder();
      let data = encoder.encode(JSON.stringify(normalized));
      try {
        let size = await IOUtils.write(this._storePath, data, {
          tmpPath: this._storePath + ".tmp",
          compress: true,
        });
        Glean.crash.compressedStoreSize.accumulate(size);
      } catch (ex) {
        // This operation might fail during shutdown, tough luck.
        console.error(ex);
      }
    })();
  },

  /**
   * Normalize an object into one fit for serialization.
   *
   * This function along with _denormalize() serve to hack around the
   * default handling of Date JSON serialization because Date serialization
   * is undefined by JSON.
   *
   * Fields ending with "Date" are assumed to contain Date instances.
   * We convert these to milliseconds since epoch on output and back to
   * Date on input.
   */
  _normalize(o) {
    let normalized = {};

    for (let k in o) {
      let v = o[k];
      if (v && k.endsWith("Date")) {
        normalized[k] = v.getTime();
      } else {
        normalized[k] = v;
      }
    }

    return normalized;
  },

  /**
   * Convert a serialized object back to its native form.
   */
  _denormalize(o) {
    let n = {};

    for (let k in o) {
      let v = o[k];
      if (v && k.endsWith("Date")) {
        n[k] = new Date(parseInt(v, 10));
      } else {
        n[k] = v;
      }
    }

    return n;
  },

  /**
   * Prune old crash data.
   *
   * Crashes without recent activity are pruned from the store so the
   * size of the store is not unbounded. If there is activity on a crash,
   * that activity will keep the crash and all its data around for longer.
   *
   * @param date
   *        (Date) The cutoff at which data will be pruned. If an entry
   *        doesn't have data newer than this, it will be pruned.
   */
  pruneOldCrashes(date) {
    for (let crash of this.crashes) {
      let newest = crash.newestDate;
      if (!newest || newest.getTime() < date.getTime()) {
        this._data.crashes.delete(crash.id);
      }
    }
  },

  /**
   * Date the store was last corrupted and required a reset.
   *
   * May be null (no corruption has ever occurred) or a Date instance.
   */
  get corruptDate() {
    return this._data.corruptDate;
  },

  /**
   * The number of distinct crashes tracked.
   */
  get crashesCount() {
    return this._data.crashes.size;
  },

  /**
   * All crashes tracked.
   *
   * This is an array of CrashRecord.
   */
  get crashes() {
    let crashes = [];
    for (let [, crash] of this._data.crashes) {
      crashes.push(new CrashRecord(crash));
    }

    return crashes;
  },

  /**
   * All tracked crashes that haven't had a ping submitted.
   *
   * This is an array of CrashRecord.
   */
  crashesWithoutPingSubmissions() {
    let crashes = [];
    for (let [, crash] of this._data.crashes) {
      if (crash.pingPending) {
        crashes.push(new CrashRecord(crash));
      }
    }

    return crashes;
  },

  /**
   * Obtain a particular crash from its ID.
   *
   * A CrashRecord will be returned if the crash exists. null will be returned
   * if the crash is unknown.
   */
  getCrash(id) {
    for (let crash of this.crashes) {
      if (crash.id == id) {
        return crash;
      }
    }

    return null;
  },

  _ensureCountsForDay(day) {
    if (!this._countsByDay.has(day)) {
      this._countsByDay.set(day, new Map());
    }
  },

  /**
   * Ensure the crash record is present in storage.
   *
   * Returns the crash record if we're allowed to store it or null
   * if we've hit the high water mark.
   *
   * @param processType
   *        (string) One of the PROCESS_TYPE constants.
   * @param crashType
   *        (string) One of the CRASH_TYPE constants.
   * @param id
   *        (string) The crash ID.
   * @param date
   *        (Date) When this crash occurred.
   * @param metadata
   *        (dictionary) Crash metadata, may be empty.
   *
   * @return null | object crash record
   */
  _ensureCrashRecord(processType, crashType, id, date, metadata) {
    if (!id) {
      // Crashes are keyed on ID, so it's not really helpful to store crashes
      // without IDs.
      return null;
    }

    let type = processType + "-" + crashType;

    if (!this._data.crashes.has(id)) {
      let day = dateToDays(date);
      this._ensureCountsForDay(day);

      let count = (this._countsByDay.get(day).get(type) || 0) + 1;
      this._countsByDay.get(day).set(type, count);

      if (
        count > this.HIGH_WATER_DAILY_THRESHOLD &&
        processType !=
          CrashManager.prototype.processTypes[
            Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT
          ]
      ) {
        return null;
      }

      // If we have an OOM size, count the crash as an OOM in addition to
      // being a main process crash.
      if (metadata && metadata.OOMAllocationSize) {
        let oomType = type + "-oom";
        let oomCount = (this._countsByDay.get(day).get(oomType) || 0) + 1;
        this._countsByDay.get(day).set(oomType, oomCount);
      }

      this._data.crashes.set(id, {
        id,
        remoteID: null,
        type,
        crashDate: date,
        submissions: new Map(),
        classifications: [],
        metadata,
        pingPending: true,
      });
    }

    let crash = this._data.crashes.get(id);
    crash.type = type;
    crash.crashDate = date;

    return crash;
  },

  /**
   * Record the occurrence of a crash.
   *
   * @param processType (string) One of the PROCESS_TYPE constants.
   * @param crashType (string) One of the CRASH_TYPE constants.
   * @param id (string) Crash ID. Likely a UUID.
   * @param date (Date) When the crash occurred.
   * @param metadata (dictionary) Crash metadata, may be empty.
   *
   * @return boolean True if the crash was recorded and false if not.
   */
  addCrash(processType, crashType, id, date, metadata) {
    return !!this._ensureCrashRecord(
      processType,
      crashType,
      id,
      date,
      metadata
    );
  },

  /**
   * @return boolean True if the remote ID was recorded and false if not.
   */
  setRemoteCrashID(crashID, remoteID) {
    let crash = this._data.crashes.get(crashID);
    if (!crash || !remoteID) {
      return false;
    }

    crash.remoteID = remoteID;
    return true;
  },

  /**
   * The pingSubmitted field indicates whether a ping was submitted to the
   * crashreporter for upload. Whether the crashreporter succeeds in sending the
   * ping or not is not relevant here: this is used to ensure each crash has a
   * ping attempted.
   *
   * @return boolean True if the pingSubmitted field was set, and false if not.
   */
  setPingSubmitted(crashID) {
    let crash = this._data.crashes.get(crashID);
    if (!crash) {
      return false;
    }
    delete crash.pingPending;
    return true;
  },

  /**
   * @param processType (string) One of the PROCESS_TYPE constants.
   * @param crashType (string) One of the CRASH_TYPE constants.
   *
   * @return array of crashes
   */
  getCrashesOfType(processType, crashType) {
    let crashes = [];
    for (let crash of this.crashes) {
      if (crash.isOfType(processType, crashType)) {
        crashes.push(crash);
      }
    }

    return crashes;
  },

  /**
   * Ensure the submission record is present in storage.
   *
   * @returns [submission, crash]
   */
  _ensureSubmissionRecord(crashID, submissionID) {
    let crash = this._data.crashes.get(crashID);
    if (!crash || !submissionID) {
      return null;
    }

    if (!crash.submissions.has(submissionID)) {
      crash.submissions.set(submissionID, {
        requestDate: null,
        responseDate: null,
        result: null,
      });
    }

    return [crash.submissions.get(submissionID), crash];
  },

  /**
   * @return boolean True if the attempt was recorded.
   */
  addSubmissionAttempt(crashID, submissionID, date) {
    let [submission, crash] = this._ensureSubmissionRecord(
      crashID,
      submissionID
    );
    if (!submission) {
      return false;
    }

    submission.requestDate = date;
    Glean.crash.submitAttempt[crash.type].add(1);
    return true;
  },

  /**
   * @return boolean True if the response was recorded.
   */
  addSubmissionResult(crashID, submissionID, date, result) {
    let crash = this._data.crashes.get(crashID);
    if (!crash || !submissionID) {
      return false;
    }
    let submission = crash.submissions.get(submissionID);
    if (!submission) {
      return false;
    }

    submission.responseDate = date;
    submission.result = result;
    Glean.crash.submitSuccess.get(crash.type, result == "ok").add(1);
    return true;
  },

  /**
   * @return boolean True if the classifications were set.
   */
  setCrashClassifications(crashID, classifications) {
    let crash = this._data.crashes.get(crashID);
    if (!crash) {
      return false;
    }

    crash.classifications = classifications;
    return true;
  },
});

/**
 * Represents an individual crash with metadata.
 *
 * This is a wrapper around the low-level anonymous JS objects that define
 * crashes. It exposes a consistent and helpful API.
 *
 * Instances of this type should only be constructured inside this module,
 * not externally. The constructor is not considered a public API.
 *
 * @param o (object)
 *        The crash's entry from the CrashStore.
 */
function CrashRecord(o) {
  this._o = o;
}

CrashRecord.prototype = Object.freeze({
  get id() {
    return this._o.id;
  },

  get remoteID() {
    return this._o.remoteID;
  },

  get crashDate() {
    return this._o.crashDate;
  },

  /**
   * Obtain the newest date in this record.
   *
   * This is a convenience getter. The returned value is used to determine when
   * to expire a record.
   */
  get newestDate() {
    // We currently only have 1 date, so this is easy.
    return this._o.crashDate;
  },

  get oldestDate() {
    return this._o.crashDate;
  },

  get type() {
    return this._o.type;
  },

  get processType() {
    const index = this.type.lastIndexOf("-");
    if (index !== -1) {
      return this.type.slice(0, index);
    }
    return this.type;
  },

  isOfType(processType, crashType) {
    return processType + "-" + crashType == this.type;
  },

  get submissions() {
    return this._o.submissions;
  },

  get classifications() {
    return this._o.classifications;
  },

  get metadata() {
    return this._o.metadata;
  },
});

ChromeUtils.defineLazyGetter(CrashManager, "_log", () =>
  lazy.Log.repository.getLogger("Crashes.CrashManager")
);

/**
 * Obtain the global CrashManager instance used by the running application.
 *
 * CrashManager is likely only ever instantiated once per application lifetime.
 * The main reason it's implemented as a reusable type is to facilitate testing.
 */
ChromeUtils.defineLazyGetter(CrashManager, "Singleton", function () {
  if (gCrashManager) {
    return gCrashManager;
  }

  gCrashManager = new CrashManager({});

  // Automatically aggregate event files shortly after startup. This
  // ensures it happens with some frequency.
  //
  // There are performance considerations here. While this is doing
  // work and could negatively impact performance, the amount of work
  // is kept small per run by periodically aggregating event files.
  // Furthermore, well-behaving installs should not have much work
  // here to do. If there is a lot of work, that install has bigger
  // issues beyond reduced performance near startup.
  gCrashManager.scheduleMaintenance(AGGREGATE_STARTUP_DELAY_MS);

  return gCrashManager;
});

export function getCrashManager() {
  return CrashManager.Singleton;
}

/**
 * Used for tests to check the crash manager is created on profile creation.
 *
 * @returns {CrashManager}
 */
export function getCrashManagerNoCreate() {
  return gCrashManager;
}
