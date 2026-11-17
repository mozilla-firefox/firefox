/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Support for Sync-related telemetry, submitted via the Glean "sync" ping.
// Also responsible for ensuring that FxA/Sync identifiers are plumbed into
// the "deletion-request" ping so they can be deleted on user request.

import { Log } from "resource://gre/modules/Log.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Async: "resource://services-common/async.sys.mjs",
  AuthenticationError: "resource://services-sync/sync_auth.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  Observers: "resource://services-common/observers.sys.mjs",
  Status: "resource://services-sync/status.sys.mjs",
  Svc: "resource://services-sync/util.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  TelemetryUtils: "resource://gre/modules/TelemetryUtils.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
  WeaveService: "moz-src:///services/sync/Weave.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

import * as constants from "resource://services-sync/constants.sys.mjs";

const log = Log.repository.getLogger("Sync.Telemetry");

const TOPICS = [
  // For tracking change to account/device identifiers.
  "fxaccounts:new_device_id",
  "fxaccounts:onlogout",
  "weave:service:ready",
  "weave:service:login:got-hashed-id",

  // For whole-of-sync metrics.
  "weave:service:sync:start",
  "weave:service:sync:finish",
  "weave:service:sync:error",

  // For individual engine metrics.
  "weave:engine:sync:start",
  "weave:engine:sync:finish",
  "weave:engine:sync:error",
  "weave:engine:sync:applied",
  "weave:engine:sync:step",
  "weave:engine:sync:uploaded",
  "weave:engine:validate:finish",
  "weave:engine:validate:error",

  "weave:telemetry:migration",
];

const EMPTY_UID = "0".repeat(32);

// The set of engines we record telemetry for - any other engines are ignored.
const ENGINES = new Set([
  "addons",
  "bookmarks",
  "clients",
  "forms",
  "history",
  "passwords",
  "prefs",
  "tabs",
  "extension-storage",
  "addresses",
  "creditcards",
]);

function tryGetMonotonicTimestamp() {
  try {
    return Services.telemetry.msSinceProcessStart();
  } catch (e) {
    log.warn("Unable to get a monotonic timestamp!");
    return -1;
  }
}

function timeDeltaFrom(monotonicStartTime) {
  let now = tryGetMonotonicTimestamp();
  if (monotonicStartTime !== -1 && now !== -1) {
    return Math.round(now - monotonicStartTime);
  }
  return -1;
}

const NS_ERROR_MODULE_BASE_OFFSET = 0x45;
const NS_ERROR_MODULE_NETWORK = 6;

// A reimplementation of NS_ERROR_GET_MODULE, which surprisingly doesn't seem
// to exist anywhere in .js code in a way that can be reused.
// This is taken from DownloadCore.sys.mjs.
function NS_ERROR_GET_MODULE(code) {
  return ((code & 0x7fff0000) >> 16) - NS_ERROR_MODULE_BASE_OFFSET;
}

// Keps track of the counts of individual records fate during a sync cycle
// The main reason this is a class is to keep track of reasons individual records
// failure reasons without huge memory overhead.
export class SyncedRecordsTelemetry {
  // applied    => number of items that should be applied.
  // failed     => number of items that failed in this sync.
  // newFailed  => number of items that failed for the first time in this sync.
  // reconciled => number of items that were reconciled.
  // failedReasons => {name, count} of reasons a record failed
  incomingCounts = {
    applied: 0,
    failed: 0,
    newFailed: 0,
    reconciled: 0,
    failedReasons: null,
  };
  outgoingCounts = { failed: 0, sent: 0, failedReasons: null };

  addIncomingFailedReason(reason) {
    if (!this.incomingCounts.failedReasons) {
      this.incomingCounts.failedReasons = [];
    }
    let transformedReason = SyncTelemetry.transformError(reason);
    // Some errors like http/nss errors don't have an error object
    // those will be caught by the higher level telemetry
    if (!transformedReason.error) {
      return;
    }

    let index = this.incomingCounts.failedReasons.findIndex(
      reasons => reasons.name === transformedReason.error
    );

    if (index >= 0) {
      this.incomingCounts.failedReasons[index].count += 1;
    } else {
      this.incomingCounts.failedReasons.push({
        name: transformedReason.error,
        count: 1,
      });
    }
  }

  addOutgoingFailedReason(reason) {
    if (!this.outgoingCounts.failedReasons) {
      this.outgoingCounts.failedReasons = [];
    }
    let transformedReason = SyncTelemetry.transformError(reason);
    // Some errors like http/nss errors don't have an error object
    // those will be caught by the higher level telemetry
    if (!transformedReason.error) {
      return;
    }
    let index = this.outgoingCounts.failedReasons.findIndex(
      reasons => reasons.name === transformedReason.error
    );
    if (index >= 0) {
      this.outgoingCounts.failedReasons[index].count += 1;
    } else {
      this.outgoingCounts.failedReasons.push({
        name: transformedReason.error,
        count: 1,
      });
    }
  }
}

// The `ErrorSanitizer` has 2 main jobs:
// * Remove PII from errors, such as the profile dir or URLs the user might
//   have visited.
// * Normalize errors so different locales or operating systems etc don't
//   generate different messages for the same underlying error.
// * [TODO] Normalize errors so environmental factors don't influence message.
//   For example, timestamps or GUIDs should be replaced with something static.
export class ErrorSanitizer {
  // Things we normalize - this isn't exhaustive, but covers the common error messages we see.
  // Eg:
  // > Win error 112 during operation write on file [profileDir]\weave\addonsreconciler.json (Espacio en disco insuficiente. )
  // > Win error 112 during operation write on file [profileDir]\weave\addonsreconciler.json (Diskte yeterli yer yok. )
  // > <snip many other translations of the error>
  // > Unix error 28 during operation write on file [profileDir]/weave/addonsreconciler.json (No space left on device)
  // These tend to crowd out other errors we might care about (eg, top 5 errors for some engines are
  // variations of the "no space left on device")

  // Note that only errors that have same-but-different errors on Windows and Unix are here - we
  // still sanitize ones that aren't in these maps to remove the translations etc - eg,
  // `ERROR_SHARING_VIOLATION` doesn't really have a unix equivalent, so doesn't appear here, but
  // we still strip the translations to avoid the variants.
  static E_PERMISSION_DENIED = "OS error [Permission denied]";
  static E_NO_FILE_OR_DIR = "OS error [File/Path not found]";

  static DOMErrorSubstitutions = {
    NotFoundError: this.E_NO_FILE_OR_DIR,
    NotAllowedError: this.E_PERMISSION_DENIED,
  };

  // IOUtils error messages include the specific nsresult error code that caused them.
  static NS_ERROR_RE = new RegExp(/ \(NS_ERROR_.*\)$/);

  static #cleanOSErrorMessage(message, error = undefined) {
    if (DOMException.isInstance(error)) {
      const sub = this.DOMErrorSubstitutions[error.name];
      message = message.replaceAll("\\", "/");
      message = message.replace(this.NS_ERROR_RE, "");
      if (sub) {
        return `${sub} ${message}`;
      }
    }

    return message;
  }

  // A regex we can use to replace the profile dir in error messages. We use a
  // regexp so we can simply replace all case-insensitive occurences.
  // This escaping function is from:
  // https://developer.mozilla.org/en/docs/Web/JavaScript/Guide/Regular_Expressions
  static reProfileDir = new RegExp(
    PathUtils.profileDir.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"),
    "gi"
  );

  /**
   * Clean an error message, removing PII and normalizing OS-specific messages.
   *
   * @param {string} message The error message
   * @param {Error?} error The error class instance, if any.
   */
  static cleanErrorMessage(message, error = undefined) {
    // There's a chance the profiledir is in the error string which is PII we
    // want to avoid including in the ping.
    message = message.replace(this.reProfileDir, "[profileDir]");
    // MSG_INVALID_URL from /dom/bindings/Errors.msg -- no way to access this
    // directly from JS.
    if (message.endsWith("is not a valid URL.")) {
      message = "<URL> is not a valid URL.";
    }
    // Try to filter things that look somewhat like a URL (in that they contain a
    // colon in the middle of non-whitespace), in case anything else is including
    // these in error messages. Note that JSON.stringified stuff comes through
    // here, so we explicitly ignore double-quotes as well.
    message = message.replace(/[^\s"]+:[^\s"]+/g, "<URL>");

    // Anywhere that's normalized the guid in errors we can easily filter
    // to make it easier to aggregate these types of errors
    message = message.replace(/<guid: ([^>]+)>/g, "<GUID>");

    return this.#cleanOSErrorMessage(message, error);
  }
}

class EngineRecord {
  constructor(name) {
    // startTime is in ms from process start, but is monotonic (unlike Date.now())
    // so we need to keep both it and when.
    this.startTime = tryGetMonotonicTimestamp();
    this.name = name;

    // This allows cases like bookmarks-buffered to have a separate name from
    // the bookmarks engine.
    let engineImpl = lazy.Weave.Service.engineManager.get(name);
    if (engineImpl && engineImpl.overrideTelemetryName) {
      this.overrideTelemetryName = engineImpl.overrideTelemetryName;
    }
  }

  toJSON() {
    let result = { name: this.overrideTelemetryName || this.name };
    let properties = [
      "took",
      "status",
      "failureReason",
      "incoming",
      "outgoing",
      "validation",
      "steps",
    ];
    for (let property of properties) {
      result[property] = this[property];
    }
    return result;
  }

  finished(error) {
    let took = timeDeltaFrom(this.startTime);
    if (took >= 0) {
      this.took = took;
    }
    if (error) {
      this.failureReason = SyncTelemetry.transformError(error);
    }
  }

  recordApplied(counts) {
    if (this.incoming) {
      log.error(
        `Incoming records applied multiple times for engine ${this.name}!`
      );
      return;
    }
    if (this.name === "clients" && !counts.failed) {
      // ignore successful application of client records
      // since otherwise they show up every time and are meaningless.
      return;
    }

    let incomingData = {};

    if (counts.failedReasons) {
      // sort the failed reasons in desc by count, then take top 10
      counts.failedReasons = counts.failedReasons
        .sort((a, b) => b.count - a.count)
        .slice(0, 10);
    }
    // Counts has extra stuff used for logging, but we only care about a few
    let properties = ["applied", "failed", "failedReasons"];
    // Only record non-zero properties and only record incoming at all if
    // there's at least one property we care about.
    for (let property of properties) {
      if (counts[property]) {
        incomingData[property] = counts[property];
        this.incoming = incomingData;
      }
    }
  }

  recordStep(stepResult) {
    let step = {
      name: stepResult.name,
    };
    if (stepResult.took >= 0) {
      step.took = Math.round(stepResult.took);
    }
    if (stepResult.counts) {
      let counts = stepResult.counts.filter(({ count }) => count > 0);
      if (counts.length) {
        step.counts = counts;
      }
    }
    if (this.steps) {
      this.steps.push(step);
    } else {
      this.steps = [step];
    }
  }

  recordValidation(validationResult) {
    if (this.validation) {
      log.error(`Multiple validations occurred for engine ${this.name}!`);
      return;
    }
    let { problems, version, took, checked } = validationResult;
    let validation = {
      version: version || 0,
      checked: checked || 0,
    };
    if (took >= 0) {
      validation.took = Math.round(took);
    }
    let summarized = problems.filter(({ count }) => count > 0);
    if (summarized.length) {
      validation.problems = summarized;
    }
    this.validation = validation;
  }

  recordValidationError(e) {
    if (this.validation) {
      log.error(`Multiple validations occurred for engine ${this.name}!`);
      return;
    }

    this.validation = {
      failureReason: SyncTelemetry.transformError(e),
    };
  }

  recordUploaded(counts) {
    if (counts.sent || counts.failed) {
      if (!this.outgoing) {
        this.outgoing = [];
      }
      if (counts.failedReasons) {
        // sort the failed reasons in desc by count, then take top 10
        counts.failedReasons = counts.failedReasons
          .sort((a, b) => b.count - a.count)
          .slice(0, 10);
      }
      this.outgoing.push({
        sent: counts.sent || undefined,
        failed: counts.failed || undefined,
        failedReasons: counts.failedReasons || undefined,
      });
    }
  }
}

// The record of a single "sync" - typically many of these are submitted in
// a single ping (ie, as a 'syncs' array)
export class SyncRecord {
  constructor(allowedEngines, why) {
    this.allowedEngines = allowedEngines;
    // Our failure reason. This property only exists in the generated ping if an
    // error actually occurred.
    this.failureReason = undefined;
    this.syncNodeType = null;
    this.when = Date.now();
    this.startTime = tryGetMonotonicTimestamp();
    this.took = 0; // will be set later.
    this.why = why;

    // All engines that have finished (ie, does not include the "current" one)
    // We omit this from the ping if it's empty.
    this.engines = [];
    // The engine that has started but not yet stopped.
    this.currentEngine = null;
  }

  toJSON() {
    let result = {
      when: this.when,
      took: this.took,
      failureReason: this.failureReason,
      status: this.status,
    };
    if (this.why) {
      result.why = this.why;
    }
    let engines = [];
    for (let engine of this.engines) {
      engines.push(engine.toJSON());
    }
    if (engines.length) {
      result.engines = engines;
    }
    return result;
  }

  finished(error) {
    this.took = timeDeltaFrom(this.startTime);
    if (this.currentEngine != null) {
      log.error(
        "Finished called for the sync before the current engine finished"
      );
      this.currentEngine.finished(null);
      this.onEngineStop(this.currentEngine.name);
    }
    if (error) {
      this.failureReason = SyncTelemetry.transformError(error);
    }

    this.syncNodeType = lazy.Weave.Service.identity.telemetryNodeType;

    // Check for engine statuses. -- We do this now, and not in engine.finished
    // to make sure any statuses that get set "late" are recorded
    for (let engine of this.engines) {
      let status = lazy.Status.engines[engine.name];
      if (status && status !== constants.ENGINE_SUCCEEDED) {
        engine.status = status;
      }
    }

    let statusObject = {};

    let serviceStatus = lazy.Status.service;
    if (serviceStatus && serviceStatus !== constants.STATUS_OK) {
      statusObject.service = serviceStatus;
      this.status = statusObject;
    }
    let syncStatus = lazy.Status.sync;
    if (syncStatus && syncStatus !== constants.SYNC_SUCCEEDED) {
      statusObject.sync = syncStatus;
      this.status = statusObject;
    }
  }

  onEngineStart(engineName) {
    if (this._shouldIgnoreEngine(engineName, false)) {
      return;
    }

    if (this.currentEngine) {
      log.error(
        `Being told that engine ${engineName} has started, but current engine ${this.currentEngine.name} hasn't stopped`
      );
      // Just discard the current engine rather than making up data for it.
    }
    this.currentEngine = new EngineRecord(engineName);
  }

  onEngineStop(engineName, error) {
    // We only care if it's the current engine if we have a current engine.
    if (this._shouldIgnoreEngine(engineName, !!this.currentEngine)) {
      return;
    }
    if (!this.currentEngine) {
      // It's possible for us to get an error before the start message of an engine
      // (somehow), in which case we still want to record that error.
      if (!error) {
        return;
      }
      log.error(
        `Error triggered on ${engineName} when no current engine exists: ${error}`
      );
      this.currentEngine = new EngineRecord(engineName);
    }
    this.currentEngine.finished(error);
    this.engines.push(this.currentEngine);
    this.currentEngine = null;
  }

  onEngineApplied(engineName, counts) {
    if (this._shouldIgnoreEngine(engineName)) {
      return;
    }
    this.currentEngine.recordApplied(counts);
  }

  onEngineStep(engineName, step) {
    if (this._shouldIgnoreEngine(engineName)) {
      return;
    }
    this.currentEngine.recordStep(step);
  }

  onEngineValidated(engineName, validationData) {
    if (this._shouldIgnoreEngine(engineName, false)) {
      return;
    }
    let engine = this.engines.find(e => e.name === engineName);
    if (
      !engine &&
      this.currentEngine &&
      engineName === this.currentEngine.name
    ) {
      engine = this.currentEngine;
    }
    if (engine) {
      engine.recordValidation(validationData);
    } else {
      log.warn(
        `Validation event triggered for engine ${engineName}, which hasn't been synced!`
      );
    }
  }

  onEngineValidateError(engineName, error) {
    if (this._shouldIgnoreEngine(engineName, false)) {
      return;
    }
    let engine = this.engines.find(e => e.name === engineName);
    if (
      !engine &&
      this.currentEngine &&
      engineName === this.currentEngine.name
    ) {
      engine = this.currentEngine;
    }
    if (engine) {
      engine.recordValidationError(error);
    } else {
      log.warn(
        `Validation failure event triggered for engine ${engineName}, which hasn't been synced!`
      );
    }
  }

  onEngineUploaded(engineName, counts) {
    if (this._shouldIgnoreEngine(engineName)) {
      return;
    }
    this.currentEngine.recordUploaded(counts);
  }

  _shouldIgnoreEngine(engineName, shouldBeCurrent = true) {
    if (!this.allowedEngines.has(engineName)) {
      log.info(
        `Notification for engine ${engineName}, but we aren't recording telemetry for it`
      );
      return true;
    }
    if (shouldBeCurrent) {
      if (!this.currentEngine || engineName != this.currentEngine.name) {
        log.info(`Notification for engine ${engineName} but it isn't current`);
        return true;
      }
    }
    return false;
  }
}

// The entire "sync ping" - it includes all the syncs, events etc recorded in
// the ping.
class SyncTelemetryImpl {
  constructor(allowedEngines) {
    log.manageLevelFromPref("services.sync.log.logger.telemetry");
    // This is accessible so we can enable custom engines during tests.
    this.allowedEngines = allowedEngines;
    this.current = null;
    this.setupObservers();

    this.payloads = [];
    this.discarded = 0;
    this.migrations = [];
    this.maxPayloadCount = lazy.Svc.PrefBranch.getIntPref(
      "telemetry.maxPayloadCount"
    );
    this.submissionInterval =
      lazy.Svc.PrefBranch.getIntPref("telemetry.submissionInterval") * 1000;
    this.lastSubmissionTime = Services.telemetry.msSinceProcessStart();
    this.lastUID = EMPTY_UID;
    this.lastSyncNodeType = null;
    this.currentSyncNodeType = null;
    // Note that the sessionStartDate is somewhat arbitrary - the telemetry
    // modules themselves just use `new Date()`. This means that our startDate
    // isn't going to be the same as the sessionStartDate in the main pings,
    // but that's OK for now - if it's a problem we'd need to change the
    // telemetry modules to expose what it thinks the sessionStartDate is.
    let sessionStartDate = new Date();
    this.sessionStartDate = lazy.TelemetryUtils.toLocalTimeISOString(
      lazy.TelemetryUtils.truncateToHours(sessionStartDate)
    );
    lazy.TelemetryController.registerSyncPingShutdown(() => this.shutdown());
  }

  sanitizeFxaDeviceId(deviceId) {
    return lazy.fxAccounts.telemetry.sanitizeDeviceId(deviceId);
  }

  prepareFxaDevices(devices) {
    // For non-sync users, the data per device is limited -- just an id and a
    // type (and not even the id yet). For sync users, if we can correctly map
    // the fxaDevice to a sync device, then we can get os and version info,
    // which would be quite unfortunate to lose.
    let extraInfoMap = new Map();
    if (this.syncIsEnabled()) {
      for (let client of this.getClientsEngineRecords()) {
        if (client.fxaDeviceId) {
          extraInfoMap.set(client.fxaDeviceId, {
            os: client.os,
            version: client.version,
            syncID: this.sanitizeFxaDeviceId(client.id),
          });
        }
      }
    }
    // Finally, sanitize and convert to the proper format.
    return devices.map(d => {
      let { os, version, syncID } = extraInfoMap.get(d.id) || {
        os: undefined,
        version: undefined,
        syncID: undefined,
      };
      return {
        id: this.sanitizeFxaDeviceId(d.id) || EMPTY_UID,
        type: d.type,
        os,
        version,
        syncID,
      };
    });
  }

  syncIsEnabled() {
    return lazy.WeaveService.enabled && lazy.WeaveService.ready;
  }

  // Separate for testing.
  getClientsEngineRecords() {
    if (!this.syncIsEnabled()) {
      throw new Error("Bug: syncIsEnabled() must be true, check it first");
    }
    return lazy.Weave.Service.clientsEngine.remoteClients;
  }

  updateFxaDevices(devices) {
    if (!devices) {
      return {};
    }
    let me = devices.find(d => d.isCurrentDevice);
    let id = me ? this.sanitizeFxaDeviceId(me.id) : undefined;
    let cleanDevices = this.prepareFxaDevices(devices);
    return { deviceID: id, devices: cleanDevices };
  }

  getFxaDevices() {
    return lazy.fxAccounts.device.recentDeviceList;
  }

  getPingJSON(reason) {
    let { devices, deviceID } = this.updateFxaDevices(this.getFxaDevices());
    return {
      why: reason,
      devices,
      discarded: this.discarded || undefined,
      syncs: this.payloads.slice(),
      uid: this.lastUID,
      syncNodeType: this.lastSyncNodeType || undefined,
      deviceID,
      sessionStartDate: this.sessionStartDate,
      migrations: !this.migrations.length ? undefined : this.migrations,
    };
  }

  _addMigrationRecord(type, info) {
    log.debug("Saw telemetry migration info", type, info);
    // Consider this in the metric definition of `syncs.migrations`.
    switch (type) {
      case "webext-storage":
        this.migrations.push({
          type: "webext-storage",
          entries: +info.entries,
          entriesSuccessful: +info.entries_successful,
          extensions: +info.extensions,
          extensionsSuccessful: +info.extensions_successful,
          openFailure: !!info.open_failure,
        });
        break;
      default:
        throw new Error("Bug: Unknown migration record type " + type);
    }
  }

  finish(reason) {
    // Note that we might be in the middle of a sync right now, and so we don't
    // want to touch this.current.
    let result = this.getPingJSON(reason);
    this.payloads = [];
    this.discarded = 0;
    this.migrations = [];
    this.submit(result);
  }

  setupObservers() {
    for (let topic of TOPICS) {
      lazy.Observers.add(topic, this, this);
    }
  }

  shutdown() {
    this.finish("shutdown");
    for (let topic of TOPICS) {
      lazy.Observers.remove(topic, this, this);
    }
  }

  submit(record) {
    if (!this.isProductionSyncUser()) {
      return false;
    }
    // We still call submit() with possibly illegal payloads so that tests can
    // know that the ping was built. We don't end up submitting them, however.
    let numMigrations = record.migrations ? record.migrations.length : 0;
    if (record.syncs.length || numMigrations) {
      log.trace(
        `submitting ${record.syncs.length} sync record(s) to telemetry`
      );

      Glean.syncs.discarded.set(record.discarded);
      Glean.syncs.hashedFxaUid.set(record.uid);
      Glean.syncs.hashedDeviceId.set(record.deviceID);
      Glean.syncs.sessionStartDate.set(record.sessionStartDate * 1000);
      Glean.syncs.syncNodeType.set(record.syncNodeType);
      // Glean object metrics forbid the use of e.g. 'type' (bug 1945220),
      // so we can't use record.syncs directly.
      let syncs = structuredClone(record.syncs);
      for (let sync of syncs) {
        if (!("devices" in sync)) {
          continue;
        }
        for (let device of sync.devices) {
          device.device_type = device.type;
          delete device.type;
        }
      }
      Glean.syncs.syncs.set(syncs);
      if (numMigrations) {
        // Glean object metrics forbid the use of e.g. 'type' (bug 1945220),
        // so we can't use record.migrations directly.
        let migrations = structuredClone(record.migrations);
        for (let migration of migrations) {
          migration.migration_type = migration.type;
          delete migration.type;
        }
        Glean.syncs.migrations.set(migrations);
      }
      GleanPings.sync.submit(record.why);
      return true;
    }
    return false;
  }

  isProductionSyncUser() {
    // If FxA isn't production then we treat sync as not being production.
    // Further, there's the deprecated "services.sync.tokenServerURI" pref we
    // need to consider - fxa doesn't consider that as if that's the only
    // pref set, they *are* running a production fxa, just not production sync.
    if (
      !lazy.FxAccounts.config.isProductionConfig() ||
      Services.prefs.prefHasUserValue("services.sync.tokenServerURI")
    ) {
      log.trace(`Not sending telemetry ping for self-hosted Sync user`);
      return false;
    }
    return true;
  }

  onSyncStarted(data) {
    const why = data && JSON.parse(data).why;
    if (this.current) {
      log.warn(
        "Observed weave:service:sync:start, but we're already recording a sync!"
      );
      // Just discard the old record, consistent with our handling of engines, above.
      this.current = null;
    }
    this.current = new SyncRecord(this.allowedEngines, why);
  }

  // We need to ensure that the telemetry `deletion-request` ping always contains the user's
  // current sync device ID, because if the user opts out of telemetry then the deletion ping
  // will be immediately triggered for sending, and we won't have a chance to fill it in later.
  // This keeps the `deletion-ping` up-to-date when the user's account state changes.
  onAccountInitOrChange() {
    // We don't submit sync pings for self-hosters, so don't need to collect their device ids either.
    if (!this.isProductionSyncUser()) {
      return;
    }
    // Awkwardly async, but no need to await. If the user's account state changes while
    // this promise is in flight, it will reject and we won't record any data in the ping.
    // (And a new notification will trigger us to try again with the new state).
    lazy.fxAccounts.device
      .getLocalId()
      .then(deviceId => {
        let sanitizedDeviceId =
          lazy.fxAccounts.telemetry.sanitizeDeviceId(deviceId);
        // In the past we did not persist the FxA metrics identifiers to disk,
        // so this might be missing until we can fetch it from the server for the
        // first time. There will be a fresh notification tirggered when it's available.
        if (sanitizedDeviceId) {
          // Sanitized device ids are 64 characters long, but telemetry limits scalar strings to 50.
          // The first 32 chars are sufficient to uniquely identify the device, so just send those.
          // It's hard to change the sync ping itself to only send 32 chars, to b/w compat reasons.
          sanitizedDeviceId = sanitizedDeviceId.substr(0, 32);
          Glean.deletionRequest.syncDeviceId.set(sanitizedDeviceId);
        }
      })
      .catch(err => {
        log.warn(
          `Failed to set sync identifiers in the deletion-request ping: ${err}`
        );
      });
  }

  // This keeps the `deletion-request` ping up-to-date when the user signs out,
  // clearing the now-nonexistent sync device id.
  onAccountLogout() {
    Glean.deletionRequest.syncDeviceId.set("");
  }

  _checkCurrent(topic) {
    if (!this.current) {
      // This is only `info` because it happens when we do a tabs "quick-write"
      log.info(
        `Observed notification ${topic} but no current sync is being recorded.`
      );
      return false;
    }
    return true;
  }

  _shouldSubmitForDataChange() {
    let newID = lazy.fxAccounts.telemetry.getSanitizedUID() || EMPTY_UID;
    let oldID = this.lastUID;
    if (
      newID != EMPTY_UID &&
      oldID != EMPTY_UID &&
      // Both are "real" uids, so we care if they've changed.
      newID != oldID
    ) {
      log.trace(
        `shouldSubmitForDataChange - uid from '${oldID}' -> '${newID}'`
      );
      return true;
    }
    // We've gone from knowing one of the ids to not knowing it (which we
    // ignore) or we've gone from not knowing it to knowing it (which is fine),
    // Now check the node type because a change there also means we should
    // submit.
    if (
      this.lastSyncNodeType &&
      this.currentSyncNodeType != this.lastSyncNodeType
    ) {
      log.trace(
        `shouldSubmitForDataChange - nodeType from '${this.lastSyncNodeType}' -> '${this.currentSyncNodeType}'`
      );
      return true;
    }
    log.trace("shouldSubmitForDataChange - no need to submit");
    return false;
  }

  maybeSubmitForDataChange() {
    if (this._shouldSubmitForDataChange()) {
      log.info(
        "Early submission of sync telemetry due to changed IDs/NodeType"
      );
      this.finish("idchange"); // this actually submits.
      this.lastSubmissionTime = Services.telemetry.msSinceProcessStart();
    }

    // Only update the last UIDs if we actually know them.
    let current_uid = lazy.fxAccounts.telemetry.getSanitizedUID();
    if (current_uid) {
      this.lastUID = current_uid;
    }
    if (this.currentSyncNodeType) {
      this.lastSyncNodeType = this.currentSyncNodeType;
    }
  }

  maybeSubmitForInterval() {
    // We want to submit the ping every `this.submissionInterval` but only when
    // there's no current sync in progress, otherwise we may end up submitting
    // the sync and the events caused by it in different pings.
    if (
      this.current == null &&
      Services.telemetry.msSinceProcessStart() - this.lastSubmissionTime >
        this.submissionInterval
    ) {
      this.finish("schedule");
      this.lastSubmissionTime = Services.telemetry.msSinceProcessStart();
    }
  }

  onSyncFinished(error) {
    if (!this.current) {
      log.warn("onSyncFinished but we aren't recording");
      return;
    }
    this.current.finished(error);
    this.currentSyncNodeType = this.current.syncNodeType;
    let current = this.current;
    this.current = null;
    this.takeTelemetryRecord(current);
  }

  takeTelemetryRecord(record) {
    // We check for "data change" before appending the current sync to payloads,
    // as it is the current sync which has the data with the new data, and thus
    // must go in the *next* submission.
    this.maybeSubmitForDataChange();
    if (this.payloads.length < this.maxPayloadCount) {
      this.payloads.push(record.toJSON());
    } else {
      ++this.discarded;
    }
    // If we are submitting due to timing, it's desirable that the most recent
    // sync is included, so we check after appending the record.
    this.maybeSubmitForInterval();
  }

  observe(subject, topic, data) {
    log.trace(`observed ${topic} ${data}`);

    switch (topic) {
      case "weave:service:ready":
      case "weave:service:login:got-hashed-id":
      case "fxaccounts:new_device_id":
        this.onAccountInitOrChange();
        break;

      case "fxaccounts:onlogout":
        this.onAccountLogout();
        break;

      /* sync itself state changes */
      case "weave:service:sync:start":
        this.onSyncStarted(data);
        break;

      case "weave:service:sync:finish":
        if (this._checkCurrent(topic)) {
          this.onSyncFinished(null);
        }
        break;

      case "weave:service:sync:error":
        // argument needs to be truthy (this should always be the case)
        this.onSyncFinished(subject || "Unknown");
        break;

      /* engine sync state changes */
      case "weave:engine:sync:start":
        if (this._checkCurrent(topic)) {
          this.current.onEngineStart(data);
        }
        break;
      case "weave:engine:sync:finish":
        if (this._checkCurrent(topic)) {
          this.current.onEngineStop(data, null);
        }
        break;

      case "weave:engine:sync:error":
        if (this._checkCurrent(topic)) {
          // argument needs to be truthy (this should always be the case)
          this.current.onEngineStop(data, subject || "Unknown");
        }
        break;

      /* engine counts */
      case "weave:engine:sync:applied":
        if (this._checkCurrent(topic)) {
          this.current.onEngineApplied(data, subject);
        }
        break;

      case "weave:engine:sync:step":
        if (this._checkCurrent(topic)) {
          this.current.onEngineStep(data, subject);
        }
        break;

      case "weave:engine:sync:uploaded":
        if (this._checkCurrent(topic)) {
          this.current.onEngineUploaded(data, subject);
        }
        break;

      case "weave:engine:validate:finish":
        if (this._checkCurrent(topic)) {
          this.current.onEngineValidated(data, subject);
        }
        break;

      case "weave:engine:validate:error":
        if (this._checkCurrent(topic)) {
          this.current.onEngineValidateError(data, subject || "Unknown");
        }
        break;

      case "weave:telemetry:migration":
        this._addMigrationRecord(data, subject);
        break;

      default:
        log.warn(`unexpected observer topic ${topic}`);
        break;
    }
  }

  // Transform an exception into a standard description. Exposed here for when
  // this module isn't directly responsible for knowing the transform should
  // happen (for example, when including an error in the |extra| field of
  // event telemetry)
  transformError(error) {
    // Certain parts of sync will use this pattern as a way to communicate to
    // processIncoming to abort the processing. However, there's no guarantee
    // this can only happen then.
    if (typeof error == "object" && error.code && error.cause) {
      error = error.cause;
    }
    if (lazy.Async.isShutdownException(error)) {
      return { name: "shutdownerror" };
    }

    if (typeof error === "string") {
      if (error.startsWith("error.")) {
        // This is hacky, but I can't imagine that it's not also accurate.
        return { name: "othererror", error };
      }
      error = ErrorSanitizer.cleanErrorMessage(error);
      return { name: "unexpectederror", error };
    }

    if (error instanceof lazy.AuthenticationError) {
      return { name: "autherror", from: error.source };
    }

    if (DOMException.isInstance(error)) {
      return {
        name: "unexpectederror",
        error: ErrorSanitizer.cleanErrorMessage(error.message, error),
      };
    }

    let httpCode =
      error.status || (error.response && error.response.status) || error.code;

    if (httpCode) {
      return { name: "httperror", code: httpCode };
    }

    if (error.failureCode) {
      return { name: "othererror", error: error.failureCode };
    }

    if (error.result) {
      // many "nsresult" errors are actually network errors - if they are
      // associated with the "network" module we assume that's true.
      // We also assume NS_ERROR_ABORT is such an error - for almost everything
      // we care about, it acually is (eg, if the connection fails early enough
      // or if we have a captive portal etc) - we don't lose anything by this
      // assumption, it's just that the error will no longer be in the "nserror"
      // category, so our analysis can still find them.
      if (
        error.result == Cr.NS_ERROR_ABORT ||
        NS_ERROR_GET_MODULE(error.result) == NS_ERROR_MODULE_NETWORK
      ) {
        return { name: "httperror", code: error.result };
      }
      return { name: "nserror", code: error.result };
    }
    // It's probably an Error object, but it also could be some
    // other object that may or may not override toString to do
    // something useful.
    let msg = String(error);
    if (msg.startsWith("[object")) {
      // Nothing useful in the default, check for a string "message" property.
      if (typeof error.message == "string") {
        msg = String(error.message);
      } else {
        // Hopefully it won't come to this...
        msg = JSON.stringify(error);
      }
    }
    return {
      name: "unexpectederror",
      error: ErrorSanitizer.cleanErrorMessage(msg),
    };
  }
}

export var SyncTelemetry = new SyncTelemetryImpl(ENGINES);
