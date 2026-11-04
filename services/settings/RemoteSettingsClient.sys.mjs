/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { Downloader } from "resource://services-settings/Attachments.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ClientEnvironmentBase:
    "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs",
  Database: "resource://services-settings/Database.sys.mjs",
  IDBHelpers: "resource://services-settings/IDBHelpers.sys.mjs",
  KintoHttpClient: "resource://services-common/kinto-http-client.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  RemoteSettingsWorker:
    "resource://services-settings/RemoteSettingsWorker.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SharedUtils: "resource://services-settings/SharedUtils.sys.mjs",
  UptakeTelemetry: "resource://services-settings/UptakeTelemetry.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => lazy.Utils.log);

/**
 * Minimalist event emitter.
 *
 * Note: we don't use `toolkit/modules/EventEmitter` because **we want** to throw
 * an error when a listener fails to execute.
 */
class EventEmitter {
  constructor(events) {
    this._listeners = new Map();
    for (const event of events) {
      this._listeners.set(event, []);
    }
  }

  /**
   * Event emitter: will execute the registered listeners in the order and
   * sequentially.
   *
   * @param {string} event    the event name
   * @param {object} payload  the event payload to call the listeners with
   */
  async emit(event, payload) {
    const callbacks = this._listeners.get(event);
    let lastError;
    for (const cb of callbacks) {
      try {
        await cb(payload);
      } catch (e) {
        lastError = e;
      }
    }
    if (lastError) {
      throw lastError;
    }
  }

  hasListeners(event) {
    return this._listeners.has(event) && !!this._listeners.get(event).length;
  }

  on(event, callback) {
    if (!this._listeners.has(event)) {
      throw new Error(`Unknown event type ${event}`);
    }
    this._listeners.get(event).push(callback);
  }

  off(event, callback) {
    if (!this._listeners.has(event)) {
      throw new Error(`Unknown event type ${event}`);
    }
    const callbacks = this._listeners.get(event);
    const i = callbacks.indexOf(callback);
    if (i < 0) {
      throw new Error(`Unknown callback`);
    } else {
      callbacks.splice(i, 1);
    }
  }
}

class APIError extends Error {}

class NetworkError extends APIError {
  constructor(e) {
    super(`Network error: ${e}`, { cause: e });
    this.name = "NetworkError";
  }
}

class NetworkOfflineError extends APIError {
  constructor() {
    super("Network is offline");
    this.name = "NetworkOfflineError";
  }
}

class ServerContentParseError extends APIError {
  constructor(e) {
    super(`Cannot parse server content: ${e}`, { cause: e });
    this.name = "ServerContentParseError";
  }
}

class BackendError extends APIError {
  constructor(e) {
    super(`Backend error: ${e}`, { cause: e });
    this.name = "BackendError";
  }
}

class BackoffError extends APIError {
  constructor(e) {
    super(`Server backoff: ${e}`, { cause: e });
    this.name = "BackoffError";
  }
}

class TimeoutError extends APIError {
  constructor(e) {
    super(`API timeout: ${e}`, { cause: e });
    this.name = "TimeoutError";
  }
}

class StorageError extends Error {
  constructor(e) {
    super(`Storage error: ${e}`, { cause: e });
    this.name = "StorageError";
  }
}

class InvalidSignatureError extends Error {
  constructor(cid, x5u, signerName) {
    let message = `Invalid content signature (${cid})`;
    if (x5u) {
      const chain = x5u.split("/").pop();
      message += ` using '${chain}' and signer ${signerName}`;
    }
    super(message);
    this.name = "InvalidSignatureError";
  }
}

class MissingSignatureError extends InvalidSignatureError {
  constructor(cid) {
    super(cid);
    this.message = `Missing signature (${cid})`;
    this.name = "MissingSignatureError";
  }
}

class CorruptedDataError extends InvalidSignatureError {
  constructor(cid) {
    super(cid);
    this.message = `Corrupted local data (${cid})`;
    this.name = "CorruptedDataError";
  }
}

class UnknownCollectionError extends Error {
  constructor(cid) {
    super(`Unknown Collection "${cid}"`);
    this.name = "UnknownCollectionError";
  }
}

class AttachmentDownloader extends Downloader {
  constructor(client) {
    super(client.bucketName, client.collectionName);
    this._client = client;
  }

  get cacheImpl() {
    const cacheImpl = {
      get: async attachmentId => {
        return this._client.db.getAttachment(attachmentId);
      },
      set: async (attachmentId, attachment) => {
        return this._client.db.saveAttachment(attachmentId, attachment);
      },
      setMultiple: async attachmentsIdsBlobs => {
        return this._client.db.saveAttachments(attachmentsIdsBlobs);
      },
      delete: async attachmentId => {
        return this._client.db.saveAttachment(attachmentId, null);
      },
      deleteMultiple: async attachmentIds => {
        return this._client.db.saveAttachments(
          attachmentIds.map(id => [id, null])
        );
      },
      prune: async excludeIds => {
        return this._client.db.pruneAttachments(excludeIds);
      },
      hasData: async () => {
        return this._client.db.hasAttachments();
      },
    };
    Object.defineProperty(this, "cacheImpl", { value: cacheImpl });
    return cacheImpl;
  }

  /**
   * Download attachment and report Telemetry on failure.
   *
   * @see Downloader.download
   */
  async download(record, options) {
    await lazy.UptakeTelemetry.report(
      lazy.UptakeTelemetry.STATUS.DOWNLOAD_START,
      {
        source: this._client.identifier,
      }
    );
    try {
      // Explicitly await here to ensure we catch a network error.
      return await super.download(record, options);
    } catch (err) {
      // Report download error.
      let status = lazy.UptakeTelemetry.STATUS.DOWNLOAD_ERROR;
      if (lazy.Utils.isOffline) {
        status = lazy.UptakeTelemetry.STATUS.NETWORK_OFFLINE_ERROR;
      } else if (/NetworkError/.test(err.message)) {
        status = lazy.UptakeTelemetry.STATUS.NETWORK_ERROR;
      }
      // If the file failed to be downloaded, report it as such in Telemetry.
      await lazy.UptakeTelemetry.report(status, {
        source: this._client.identifier,
        errorName: err.name,
      });
      throw err;
    }
  }

  /**
   * Delete all downloaded records attachments.
   *
   * Note: the list of attachments to be deleted is based on the
   * current list of records.
   */
  async deleteAll() {
    let allRecords = await this._client.db.list();
    const idsToDelete = allRecords.filter(r => !!r.attachment).map(r => r.id);
    if (idsToDelete.length) {
      await this.cacheImpl.deleteMultiple(idsToDelete);
    }
  }
}

export class RemoteSettingsClient extends EventEmitter {
  static get APIError() {
    return APIError;
  }
  static get NetworkError() {
    return NetworkError;
  }
  static get NetworkOfflineError() {
    return NetworkOfflineError;
  }
  static get ServerContentParseError() {
    return ServerContentParseError;
  }
  static get BackendError() {
    return BackendError;
  }
  static get BackoffError() {
    return BackoffError;
  }
  static get TimeoutError() {
    return TimeoutError;
  }
  static get StorageError() {
    return StorageError;
  }
  static get InvalidSignatureError() {
    return InvalidSignatureError;
  }
  static get MissingSignatureError() {
    return MissingSignatureError;
  }
  static get CorruptedDataError() {
    return CorruptedDataError;
  }
  static get UnknownCollectionError() {
    return UnknownCollectionError;
  }
  static get EmptyDatabaseError() {
    return lazy.Database.EmptyDatabaseError;
  }

  /**
   * RemoteSettingsClient constructor.
   *
   * options.filterCreator is an optional function returning a filter object
   * which can map and exclude the entries returned from `.get()`. You often
   * want to set this to the default filter creator `jexlFilterCreator`.
   * The function needs to have the shape
   * `async (environment, collectionName) => RemoteSettingsEntryFilter`, where
   * `RemoteSettingsEntryFilter` refers to an interface with a single method:
   * `async filterEntry(entry)`. This method should return either the (mapped)
   * entry or a falsy value if the entry should be filtered out.
   */
  constructor(
    collectionName,
    {
      bucketName = AppConstants.REMOTE_SETTINGS_DEFAULT_BUCKET,
      signerName,
      filterCreator,
      localFields = [],
      keepAttachmentsIds = [],
      lastCheckTimePref,
    } = {}
  ) {
    // Remote Settings cannot be used in child processes (no access to disk,
    // easily killed, isolated observer notifications etc.).
    // Since our goal here is to prevent consumers to instantiate while developing their
    // feature, throwing in Nightly only is enough, and prevents unexpected crashes
    // in release or beta.
    if (
      !AppConstants.RELEASE_OR_BETA &&
      Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT
    ) {
      throw new Error(
        "Cannot instantiate Remote Settings client in child processes."
      );
    }

    super(["sync"]); // emitted events

    this.collectionName = collectionName;
    // Client is constructed with the raw bucket name (eg. "main", "security-state", "blocklist")
    // The `bucketName` will contain the `-preview` suffix if the preview mode is enabled.
    this.bucketName = lazy.Utils.actualBucketName(bucketName);
    this.signerName = signerName;
    this.filterCreator = filterCreator;
    this.localFields = localFields;
    this.keepAttachmentsIds = keepAttachmentsIds;
    this._lastCheckTimePref = lastCheckTimePref;
    this._verifier = null;
    this._syncRunning = false;

    // This attribute allows signature verification to be disabled, when running tests
    // or when pulling data from a dev server.
    this.verifySignature = AppConstants.REMOTE_SETTINGS_VERIFY_SIGNATURE;
  }

  #lazy = XPCOMUtils.declareLazy({
    db: () => new lazy.Database(this.identifier),
    attachments: () => new AttachmentDownloader(this),
  });

  get db() {
    return this.#lazy.db;
  }

  get attachments() {
    return this.#lazy.attachments;
  }

  /**
   * Internal method to refresh the client bucket name after the preview mode
   * was toggled.
   *
   * See `RemoteSettings.enabledPreviewMode()`.
   */
  refreshBucketName() {
    this.bucketName = lazy.Utils.actualBucketName(this.bucketName);
    this.db.identifier = this.identifier;
  }

  get identifier() {
    return `${this.bucketName}/${this.collectionName}`;
  }

  get lastCheckTimePref() {
    return (
      this._lastCheckTimePref ||
      `services.settings.${this.bucketName}.${this.collectionName}.last_check`
    );
  }

  httpClient() {
    const api = new lazy.KintoHttpClient(lazy.Utils.SERVER_URL, {
      fetchFunc: lazy.Utils.fetch, // Use fetch() wrapper.
    });
    return api.bucket(this.bucketName).collection(this.collectionName);
  }

  /**
   * Retrieve the collection timestamp for the last synchronization.
   * This is an opaque and comparable value assigned automatically by
   * the server.
   *
   * @returns {Promise<number>}
   *          The timestamp in milliseconds, returns -1 if retrieving
   *          the timestamp from the kinto collection fails.
   */
  async getLastModified() {
    let timestamp = -1;
    try {
      timestamp = await this.db.getLastModified();
    } catch (err) {
      lazy.console.warn(
        `Error retrieving the getLastModified timestamp from ${this.identifier} RemoteSettingsClient`,
        err
      );
    }

    return timestamp;
  }

  /**
   * Lists settings.
   *
   * @param  {object} [options]
   *   The options object.
   * @param  {object} [options.filters]
   *   Filter the results (default: `{}`).
   * @param  {string} [options.order]
   *   The order to apply (eg. `"-last_modified"`).
   * @param  {boolean} [options.dumpFallback]
   *   Fallback to dump data if read of local DB fails (default: `true`).
   * @param  {boolean} [options.emptyListFallback]
   *   Fallback to empty list if no dump data and read of local DB fails (default: `true`).
   * @param  {boolean} [options.loadDumpIfNewer]
   *   Use dump data if it is newer than local data (default: `true`).
   * @param  {boolean} [options.forceSync]
   *   Always synchronize from server before returning results (default: `false`).
   * @param  {boolean} [options.syncIfEmpty]
   *   Synchronize from server if local data is empty (default: `true`).
   * @param  {boolean} [options.verifySignature]
   *   Verify the signature of the local data (default: `false`).
   * @return {Promise<object[]>}
   */
  // eslint-disable-next-line complexity
  async get(options = {}) {
    const {
      filters = {},
      order = "", // not sorted by default.
      dumpFallback = true,
      emptyListFallback = true,
      forceSync = false,
      loadDumpIfNewer = true,
      syncIfEmpty = true,
    } = options;
    let { verifySignature = false } = options;

    const hasParallelCall = !!this._importingPromise;
    let data;
    try {
      let lastModified = forceSync ? null : await this.db.getLastModified();
      let hasLocalData = lastModified !== null;

      if (forceSync) {
        if (!this._importingPromise) {
          this._importingPromise = (async () => {
            await this.sync({ sendEvents: false, trigger: "forced" });
            return true; // No need to re-verify signature after sync.
          })();
        }
      } else if (!syncIfEmpty && !hasLocalData && !emptyListFallback) {
        // The local database is empty, we neither want to sync nor fallback to an empty list.
        throw new RemoteSettingsClient.EmptyDatabaseError(this.identifier);
      } else if (!syncIfEmpty && !hasLocalData && verifySignature) {
        // The local database is empty and we want to verify the signature.
        throw new RemoteSettingsClient.MissingSignatureError(this.identifier);
      } else if (syncIfEmpty && !hasLocalData) {
        // .get() was called before we had the chance to synchronize the local database.
        // We'll try to avoid returning an empty list.
        if (!this._importingPromise) {
          // Prevent parallel loading when .get() is called multiple times.
          this._importingPromise = (async () => {
            const importedFromDump = lazy.Utils.LOAD_DUMPS
              ? await this._importJSONDump()
              : -1;
            if (importedFromDump < 0) {
              // There is no JSON dump to load, force a synchronization from the server.
              // We don't want the "sync" event to be sent, since some consumers use `.get()`
              // in "sync" callbacks. See Bug 1761953
              lazy.console.debug(
                `${this.identifier} Local DB is empty, pull data from server`
              );
              const waitedAt = ChromeUtils.now();
              const pulled = await lazy.RemoteSettings.pullStartupBundle();
              // If collection is not part of startup bundle, then sync it individually.
              if (!pulled.includes(this.identifier)) {
                lazy.console.debug(
                  `${this.identifier} was not part of startup bundle. Force a sync`
                );
                await this.sync({ loadDump: false, sendEvents: false });
              }
              ChromeUtils.addProfilerMarker(
                "remote-settings:get:sync",
                waitedAt,
                "get() with syncIfEmpty"
              );

              const durationMilliseconds = ChromeUtils.now() - waitedAt;
              lazy.console.debug(
                `${this.identifier} Waited ${durationMilliseconds}ms for 'syncIfEmpty' in 'get()'`
              );
            }
            // Return `true` to indicate we don't need to `verifySignature`,
            // since a trusted dump was loaded or a signature verification
            // happened during synchronization.
            return true;
          })();
        } else {
          lazy.console.debug(`${this.identifier} Awaiting existing import.`);
        }
      } else if (hasLocalData && loadDumpIfNewer && lazy.Utils.LOAD_DUMPS) {
        // Check whether the local data is older than the packaged dump.
        // If it is and we are on production, load the packaged dump (which
        // overwrites the local data).
        let lastModifiedDump = await lazy.Utils.getLocalDumpLastModified(
          this.bucketName,
          this.collectionName
        );
        if (lastModified < lastModifiedDump) {
          lazy.console.debug(
            `${this.identifier} Local DB is stale (${lastModified}), using dump instead (${lastModifiedDump})`
          );
          if (!this._importingPromise) {
            // As part of importing, any existing data is wiped.
            this._importingPromise = (async () => {
              const importedFromDump = await this._importJSONDump();
              // Return `true` to skip signature verification if a dump was found.
              // The dump can be missing if a collection is listed in the timestamps summary file,
              // because its dump is present in the source tree, but the dump was not
              // included in the `package-manifest.in` file. (eg. android, thunderbird)
              return importedFromDump >= 0;
            })();
          } else {
            lazy.console.debug(`${this.identifier} Awaiting existing import.`);
          }
        }
      }

      if (this._importingPromise) {
        try {
          if (await this._importingPromise) {
            // No need to verify signature, because either we've just loaded a trusted
            // dump (here or in a parallel call), or it was verified during sync.
            verifySignature = false;
          }
        } catch (e) {
          if (!hasParallelCall) {
            // Sync or load dump failed. Throw.
            throw e;
          }
          // Report error, but continue because there could have been data
          // loaded from a parallel call.
          lazy.console.error(e);
        } finally {
          // then delete this promise again, as now we should have local data:
          delete this._importingPromise;
        }
      }

      // If there is no data for this collection, it will throw an error.
      data = await this.db.list({ filters, order });
    } catch (e) {
      // If the local DB is empty or cannot be read (for unknown reasons, Bug 1649393)
      // or sync failed, we fallback to the packaged data, and filter/sort in memory.
      if (!dumpFallback) {
        throw e;
      }
      if (
        e instanceof RemoteSettingsClient.EmptyDatabaseError &&
        emptyListFallback
      ) {
        // If consumer requested an empty list fallback, no need to raise attention if no data in DB.
        lazy.console.debug(e);
      } else if (Services.startup.shuttingDown) {
        lazy.console.debug(
          `${this.identifier} read interrupted by shutdown`,
          e
        );
      } else {
        // Report error, and continue with trying to load the binary dump.
        lazy.console.error(e);
      }
      ({ data } = await lazy.SharedUtils.loadJSONDump(
        this.bucketName,
        this.collectionName
      ));
      if (data !== null) {
        lazy.console.info(`${this.identifier} falling back to JSON dump`);
      } else if (emptyListFallback) {
        lazy.console.info(
          `${this.identifier} no dump fallback, return empty list`
        );
        data = [];
      } else {
        // Obtaining the records failed, there is no dump, and we don't fallback
        // to an empty list. Throw the original error.
        throw e;
      }
      if (!lazy.ObjectUtils.isEmpty(filters)) {
        data = data.filter(r => lazy.Utils.filterObject(filters, r));
      }
      if (order) {
        data = lazy.Utils.sortObjects(order, data);
      }
      // No need to verify signature on JSON dumps.
      // If local DB cannot be read, then we don't even try to do anything,
      // we return results early.
      return this._filterEntries(data);
    }

    if (this.verifySignature && verifySignature) {
      lazy.console.debug(
        `${this.identifier} verify signature of local data on read`
      );
      const allData = lazy.ObjectUtils.isEmpty(filters)
        ? data
        : await this.db.list();
      const localRecords = allData.map(r => this._cleanLocalFields(r));
      const timestamp = await this.db.getLastModified();
      let metadata = await this.db.getMetadata();
      if (syncIfEmpty && lazy.ObjectUtils.isEmpty(metadata)) {
        // No sync occured yet, may have records from dump but no metadata.
        // We don't want the "sync" event to be sent, since some consumers use `.get()`
        // in "sync" callbacks. See Bug 1761953
        await this.sync({ loadDump: false, sendEvents: false });
        metadata = await this.db.getMetadata();
      }
      // Will throw MissingSignatureError if no metadata and `syncIfEmpty` is false.
      await this.validateCollectionSignature(localRecords, timestamp, metadata);
    }

    // Filter the records based on `this.filterCreator` results.
    const final = await this._filterEntries(data);
    if (final.length != data.length) {
      lazy.console.debug(
        `${this.identifier} ${final.length}/${data.length} records after filtering.`
      );
    } else {
      lazy.console.debug(`${this.identifier} ${data.length} records.`);
    }
    return final;
  }

  /**
   * Synchronize the local database with the remote server.
   *
   * @param {object} options See #maybeSync() options.
   */
  async sync(options) {
    if (lazy.Utils.shouldSkipRemoteActivity) {
      lazy.console.debug(`${this.identifier} Skip remote sync.`);
      return;
    }

    // We want to know which timestamp we are expected to obtain in order to leverage
    // cache busting. We don't provide ETag because we don't want a 304.
    const { changes } = await lazy.Utils.fetchLatestChanges(
      lazy.Utils.SERVER_URL,
      {
        filters: {
          collection: this.collectionName,
          bucket: this.bucketName,
        },
      }
    );
    if (changes.length === 0) {
      throw new RemoteSettingsClient.UnknownCollectionError(this.identifier);
    }
    // According to API, there will be one only (fail if not).
    const [{ last_modified: expectedTimestamp }] = changes;

    await this.maybeSync(expectedTimestamp, { ...options, trigger: "forced" });
  }

  /**
   * Synchronize the local database with the remote server, **only if necessary**.
   *
   * @param {number} expectedTimestamp
   *   The lastModified date (on the server) for the remote collection. This will
   *   be compared to the local timestamp, and will be used for cache busting if
   *   local data is out of date.
   * @param {object} [options]
   *   additional advanced options.
   * @param {boolean} [options.loadDump]
   *   load initial dump from disk on first sync (default: true if server is prod)
   * @param {boolean} [options.sendEvents]
   *   send `"sync"` events (default: `true`)
   * @param {string} [options.trigger]
   *   label to identify what triggered this sync (eg. ``"timer"``, default: `"manual"`)
   * @return {Promise<void>}
   *   which rejects on sync or process failure.
   */
  // eslint-disable-next-line complexity
  async maybeSync(expectedTimestamp, options = {}) {
    // Should the clients try to load JSON dump? (mainly disabled in tests)
    const {
      loadDump = lazy.Utils.LOAD_DUMPS,
      trigger = "manual",
      sendEvents = true,
    } = options;

    // Make sure we don't run several synchronizations in parallel, mainly
    // in order to avoid race conditions in "sync" events listeners.
    if (this._syncRunning) {
      lazy.console.warn(`${this.identifier} sync already running`);
      return;
    }

    // Prevent network requests and IndexedDB calls to be initiated
    // during shutdown.
    if (Services.startup.shuttingDown) {
      lazy.console.warn(`${this.identifier} sync interrupted by shutdown`);
      return;
    }

    this._syncRunning = true;

    await lazy.UptakeTelemetry.report(lazy.UptakeTelemetry.STATUS.SYNC_START, {
      source: this.identifier,
      trigger,
    });

    let importedFromDump = [];
    const startedAt = new Date();
    let reportStatus = null;
    let thrownError = null;
    let ageSeconds;
    try {
      // If network is offline, we can't synchronize.
      if (lazy.Utils.isOffline) {
        throw new RemoteSettingsClient.NetworkOfflineError();
      }

      // Read last timestamp and local data before sync.
      let collectionLastModified = await this.db.getLastModified();
      const hasLocalData = collectionLastModified !== null;
      // Local data can contain local fields, strip them.
      let localRecords = hasLocalData
        ? (await this.db.list()).map(r => this._cleanLocalFields(r))
        : null;
      const localMetadata = await this.db.getMetadata();

      // If there is no data currently in the collection, attempt to import
      // initial data from the application defaults.
      // This allows to avoid synchronizing the whole collection content on
      // cold start.
      if (!hasLocalData && loadDump) {
        try {
          const imported = await this._importJSONDump();
          // The worker only returns an integer. List the imported records to build the sync event.
          if (imported > 0) {
            lazy.console.debug(
              `${this.identifier} ${imported} records loaded from JSON dump`
            );
            importedFromDump = await this.db.list();
            // Local data is the data loaded from dump. We will need this later
            // to compute the sync result.
            localRecords = importedFromDump;
          }
          collectionLastModified = await this.db.getLastModified();
        } catch (e) {
          // Report but go-on.
          console.error(e);
        }
      }
      let syncResult;
      try {
        // Is local timestamp up to date with the server?
        if (expectedTimestamp == collectionLastModified) {
          lazy.console.debug(`${this.identifier} local data is up-to-date`);
          reportStatus = lazy.UptakeTelemetry.STATUS.UP_TO_DATE;

          // If the data is up-to-date but don't have metadata (records loaded from dump),
          // we fetch them and validate the signature immediately.
          if (this.verifySignature && lazy.ObjectUtils.isEmpty(localMetadata)) {
            lazy.console.debug(`${this.identifier} pull collection metadata`);
            let metadata;
            ({ metadata, ageSeconds } = await this._fetchChangeset(
              expectedTimestamp,
              expectedTimestamp
            ));
            await this.db.importChanges(metadata);
            // We don't bother validating the signature if the dump was just loaded. We do
            // if the dump was loaded at some other point (eg. from .get()).
            if (this.verifySignature && !importedFromDump.length) {
              lazy.console.debug(
                `${this.identifier} verify signature of local data`
              );
              await this.validateCollectionSignature(
                localRecords,
                collectionLastModified,
                metadata
              );
            }
          }

          // Since the data is up-to-date, if we didn't load any dump then we're done here.
          if (!importedFromDump.length) {
            return;
          }
          // Otherwise we want to continue with sending the sync event to notify about the created records.
          syncResult = {
            current: importedFromDump,
            created: importedFromDump,
            updated: [],
            deleted: [],
          };
        } else {
          // Local data is either outdated or tampered.
          // In both cases we will fetch changes from server,
          // and make sure we overwrite local data.
          ({ syncResult, ageSeconds } = await this._importChanges(
            localRecords,
            collectionLastModified,
            localMetadata,
            expectedTimestamp
          ));
          if (sendEvents && this.hasListeners("sync")) {
            // If we have listeners for the "sync" event, then compute the lists of changes.
            // The records imported from the dump should be considered as "created" for the
            // listeners.
            const importedById = importedFromDump.reduce((acc, r) => {
              acc.set(r.id, r);
              return acc;
            }, new Map());
            // Deleted records should not appear as created.
            syncResult.deleted.forEach(r => importedById.delete(r.id));
            // Records from dump that were updated should appear in their newest form.
            syncResult.updated.forEach(u => {
              if (importedById.has(u.old.id)) {
                importedById.set(u.old.id, u.new);
              }
            });
            syncResult.created = syncResult.created.concat(
              Array.from(importedById.values())
            );
          }

          // When triggered from the daily timer, and if the sync was successful, and once
          // all sync listeners have been executed successfully, we prune potential
          // obsolete attachments that may have been left in the local cache.
          if (trigger == "timer") {
            const deleted = await this.attachments.prune(
              this.keepAttachmentsIds
            );
            if (deleted > 0) {
              lazy.console.warn(
                `${this.identifier} Pruned ${deleted} obsolete attachments`
              );
            }
          }
        }
      } catch (e) {
        if (e instanceof InvalidSignatureError) {
          // Signature verification failed during synchronization.
          reportStatus =
            e instanceof CorruptedDataError
              ? lazy.UptakeTelemetry.STATUS.CORRUPTION_ERROR
              : lazy.UptakeTelemetry.STATUS.SIGNATURE_ERROR;
          // If sync fails with a signature error, it's likely that our
          // local data has been modified in some way.
          // We will attempt to fix this by retrieving the whole
          // remote collection.
          try {
            lazy.console.warn(
              `${this.identifier} Signature verified failed. Retry from scratch`
            );
            ({ syncResult, ageSeconds } = await this._importChanges(
              localRecords,
              collectionLastModified,
              localMetadata,
              expectedTimestamp,
              { retry: true }
            ));
          } catch (ex) {
            // If the signature fails again, or if an error occured during wiping out the
            // local data, then we report it as a *signature retry* error.
            reportStatus = lazy.UptakeTelemetry.STATUS.SIGNATURE_RETRY_ERROR;
            throw ex;
          }
        } else {
          // The sync has thrown for other reason than signature verification.
          // Obtain a more precise error than original one.
          const adjustedError = this._adjustedError(e);
          // Default status for errors at this step is SYNC_ERROR.
          reportStatus = this._telemetryFromError(adjustedError, {
            default: lazy.UptakeTelemetry.STATUS.SYNC_ERROR,
          });
          throw adjustedError;
        }
      }
      if (sendEvents) {
        // Filter the synchronization results using `filterCreator` (ie. JEXL).
        const filteredSyncResult = await this._filterSyncResult(syncResult);
        // If every changed entry is filtered, we don't even fire the event.
        if (filteredSyncResult) {
          try {
            await this.emit("sync", { data: filteredSyncResult });
          } catch (e) {
            reportStatus = lazy.UptakeTelemetry.STATUS.APPLY_ERROR;
            throw e;
          }
        } else {
          // Check if `syncResult` had changes before filtering to adjust logging message.
          const wasFiltered =
            syncResult.created.length +
              syncResult.updated.length +
              syncResult.deleted.length >
            0;
          if (wasFiltered) {
            lazy.console.info(
              `${this.identifier} All sync changes are filtered by JEXL expressions`
            );
          } else {
            lazy.console.info(`${this.identifier} No changes during sync`);
          }
        }
      }
    } catch (e) {
      thrownError = e;
      // Obtain a more precise error than original one.
      const adjustedError = this._adjustedError(e);
      // If browser is shutting down, then we can report a specific status.
      // (eg. IndexedDB will abort transactions)
      if (Services.startup.shuttingDown) {
        reportStatus = lazy.UptakeTelemetry.STATUS.SHUTDOWN_ERROR;
      }
      // If no Telemetry status was determined yet (ie. outside sync step),
      // then introspect error, default status at this step is UNKNOWN.
      else if (reportStatus == null) {
        reportStatus = this._telemetryFromError(adjustedError, {
          default: lazy.UptakeTelemetry.STATUS.UNKNOWN_ERROR,
        });
      }
      throw e;
    } finally {
      const durationMilliseconds = new Date() - startedAt;
      // No error was reported, this is a success!
      if (reportStatus === null) {
        reportStatus = lazy.UptakeTelemetry.STATUS.SUCCESS;
      }
      // Report success/error status to Telemetry.
      let reportArgs = {
        source: this.identifier,
        trigger,
        duration: durationMilliseconds,
      };
      if (ageSeconds !== undefined) {
        reportArgs.age = ageSeconds;
      }
      // In Bug 1617133, we will try to break down specific errors into
      // more precise statuses by reporting the JavaScript error name
      // ("TypeError", etc.) to Telemetry.
      if (
        thrownError !== null &&
        [
          lazy.UptakeTelemetry.STATUS.SYNC_ERROR,
          lazy.UptakeTelemetry.STATUS.CUSTOM_1_ERROR, // IndexedDB.
          lazy.UptakeTelemetry.STATUS.UNKNOWN_ERROR,
          lazy.UptakeTelemetry.STATUS.SHUTDOWN_ERROR,
        ].includes(reportStatus)
      ) {
        // List of possible error names for IndexedDB:
        // https://searchfox.org/mozilla-central/rev/49ed791/dom/base/DOMException.cpp#28-53
        reportArgs = { ...reportArgs, errorName: thrownError.name };
      }

      await lazy.UptakeTelemetry.report(reportStatus, reportArgs);

      lazy.console.debug(`${this.identifier} sync status is ${reportStatus}`);
      this._syncRunning = false;
    }
  }

  /**
   * Return a more precise error instance, based on the specified
   * error and its message.
   *
   * @param {Error} e the original error
   * @returns {Error}
   */
  _adjustedError(e) {
    if (/unparseable/.test(e.message)) {
      return new RemoteSettingsClient.ServerContentParseError(e);
    }
    if (/NetworkError/.test(e.message)) {
      return new RemoteSettingsClient.NetworkError(e);
    }
    if (/Timeout/.test(e.message)) {
      return new RemoteSettingsClient.TimeoutError(e);
    }
    if (/HTTP 5??/.test(e.message)) {
      return new RemoteSettingsClient.BackendError(e);
    }
    if (/Backoff/.test(e.message)) {
      return new RemoteSettingsClient.BackoffError(e);
    }
    if (
      // Errors from kinto.js IDB adapter.
      e instanceof lazy.IDBHelpers.IndexedDBError ||
      // Other IndexedDB errors (eg. RemoteSettingsWorker).
      /IndexedDB/.test(e.message)
    ) {
      return new RemoteSettingsClient.StorageError(e);
    }
    return e;
  }

  /**
   * Determine the Telemetry uptake status based on the specified
   * error.
   */
  _telemetryFromError(e, options = { default: null }) {
    let reportStatus = options.default;

    if (e instanceof RemoteSettingsClient.NetworkOfflineError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.NETWORK_OFFLINE_ERROR;
    } else if (e instanceof lazy.IDBHelpers.ShutdownError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.SHUTDOWN_ERROR;
    } else if (e instanceof RemoteSettingsClient.ServerContentParseError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.PARSE_ERROR;
    } else if (e instanceof RemoteSettingsClient.NetworkError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.NETWORK_ERROR;
    } else if (e instanceof RemoteSettingsClient.TimeoutError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.TIMEOUT_ERROR;
    } else if (e instanceof RemoteSettingsClient.BackendError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.SERVER_ERROR;
    } else if (e instanceof RemoteSettingsClient.BackoffError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.BACKOFF;
    } else if (e instanceof RemoteSettingsClient.StorageError) {
      reportStatus = lazy.UptakeTelemetry.STATUS.CUSTOM_1_ERROR;
    }

    return reportStatus;
  }

  /**
   * Import the JSON files from services/settings/dump into the local DB.
   */
  async _importJSONDump() {
    lazy.console.info(`${this.identifier} try to restore dump`);
    const result = await lazy.RemoteSettingsWorker.importJSONDump(
      this.bucketName,
      this.collectionName
    );
    if (result < 0) {
      lazy.console.debug(`${this.identifier} no dump available`);
    } else {
      lazy.console.info(
        `${this.identifier} imported ${result} records from dump`
      );
    }
    return result;
  }

  /**
   * Fetch the signature info from the collection metadata and verifies that the
   * local set of records has the same.
   *
   * @param {object[]} records
   *   The list of records to validate.
   * @param {number} timestamp
   *   The timestamp associated with the list of remote records.
   * @param {object} metadata
   *   The collection metadata, that contains the signature payload.
   */
  async validateCollectionSignature(records, timestamp, metadata) {
    if (
      !metadata?.signatures ||
      !Array.isArray(metadata.signatures) ||
      metadata.signatures.length === 0
    ) {
      throw new MissingSignatureError(this.identifier);
    }

    if (!this._verifier) {
      this._verifier = Cc[
        "@mozilla.org/security/contentsignatureverifier;1"
      ].createInstance(Ci.nsIContentSignatureVerifier);
    }

    // Merge remote records with local ones and serialize as canonical JSON.
    const serialized = await lazy.RemoteSettingsWorker.canonicalStringify(
      records,
      timestamp
    );

    // Iterate over the list of signatures until we find a valid one.
    const thrownErrors = [];
    const { signatures } = metadata;
    for (const sig of signatures) {
      // This is a content-signature field from an autograph response.
      const { x5u, signature, mode } = sig;
      if (!x5u || !signature || (mode && mode !== "p384ecdsa")) {
        lazy.console.warn(
          `${this.identifier} ignore unsupported signature entry in metadata`
        );
        continue;
      }
      const certChain = await (await lazy.Utils.fetch(x5u)).text();
      lazy.console.debug(`${this.identifier} verify signature using ${x5u}`);

      if (
        await this._verifier.asyncVerifyContentSignature(
          serialized,
          "p384ecdsa=" + signature,
          certChain,
          this.signerName,
          lazy.Utils.CERT_CHAIN_ROOT_IDENTIFIER
        )
      ) {
        // Signature is valid, exit!
        return;
      }
      // Try next signature.
      thrownErrors.push(
        new InvalidSignatureError(this.identifier, x5u, this.signerName)
      );
    }

    // We know that the list of signatures is not empty, so if we are here
    // it means that none was valid, or that none was usable at all.
    throw thrownErrors[0] ?? new MissingSignatureError(this.identifier);
  }

  /**
   * This method is in charge of fetching data from server, applying the diff-based
   * changes to the local DB, validating the signature, and computing a synchronization
   * result with the list of creation, updates, and deletions.
   *
   * @param {object[]} localRecords
   *   Current list of records in local DB.
   * @param {number} localTimestamp
   *   Current timestamp in local DB.
   * @param {object} localMetadata
   *   Current metadata in local DB.
   * @param {number} expectedTimestamp
   *   Cache busting of collection metadata
   * @param {object} [options]
   * @param {boolean} [options.retry]
   *   Whether this method is called in the retry situation.
   * @returns {Promise<{syncResult: object, ageSeconds: number}>}
   *   the synchronization result and the age of the pulled data in seconds.
   */
  async _importChanges(
    localRecords,
    localTimestamp,
    localMetadata,
    expectedTimestamp,
    options = {}
  ) {
    const hasLocalData = localTimestamp !== null;
    const { retry = false } = options;
    // On retry, we fully re-fetch the collection (no `?_since`).
    const since = retry || !hasLocalData ? undefined : localTimestamp;

    // Define an executor that will verify the signature of the local data.
    const verifySignatureLocalData = (resolve, reject) => {
      if (!hasLocalData) {
        resolve(false);
        return;
      }
      lazy.console.debug(
        `${this.identifier} verify local data before importing remote`
      );
      this.validateCollectionSignature(
        localRecords,
        localTimestamp,
        localMetadata
      )
        .then(() => resolve(true))
        .catch(err => {
          if (err instanceof InvalidSignatureError) {
            lazy.console.debug(`${this.identifier} previous data was invalid`);
            resolve(false);
          } else {
            // If it fails for other reason, keep original error and give up.
            reject(err);
          }
        });
    };

    // Fetch collection metadata and list of changes from server.
    lazy.console.debug(
      `${this.identifier} Fetch changes from server (expected=${expectedTimestamp}, since=${since})`
    );
    const { metadata, remoteTimestamp, remoteRecords, ageSeconds } =
      await this._fetchChangeset(expectedTimestamp, since);

    lazy.console.debug(
      `${this.identifier} local timestamp: ${localTimestamp}, remote: ${remoteTimestamp} (expected: ${expectedTimestamp})`
    );

    if (remoteTimestamp < localTimestamp) {
      // This should never happen. Unless the CDN serves stale data.
      // If the local data is valid, then we can safely ignore this stage remote changeset.
      let localTrustworthy = false;
      try {
        localTrustworthy = await new Promise(verifySignatureLocalData);
      } catch (exc) {
        // Verifying the local data failed for another reason than an invalid
        // signature (eg. its cert chain could not be fetched). Consider it
        // untrustworthy and carry on with reset/import.
        lazy.console.error(exc);
      }
      if (localTrustworthy) {
        lazy.console.info(`${this.identifier} CDN served staled data, ignore.`);
        return {
          syncResult: {
            current: localRecords,
            created: [],
            updated: [],
            deleted: [],
          },
          ageSeconds,
        };
      }
      // Otherwise, continue with importing, since we prefer stale data over corrupt/tempered.
      lazy.console.warn(
        `${this.identifier} CDN served staled data, but local data is corrupted, import anyway.`
      );
    }

    await this.db.importChanges(metadata, remoteTimestamp, remoteRecords, {
      clear: retry,
    });

    // Read the new local data, after updating.
    const newLocal = await this.db.list();
    const newRecords = newLocal.map(r => this._cleanLocalFields(r));
    // And verify the signature on what is now stored.
    if (this.verifySignature) {
      try {
        await this.validateCollectionSignature(
          newRecords,
          remoteTimestamp,
          metadata
        );
      } catch (e) {
        lazy.console.error(
          `${this.identifier} Signature failed ${retry ? "again" : ""} ${e}`
        );

        // Any verification failure, invalid signature, malformed signature,
        // or a failed x5u cert-chain fetch, must roll back the records just
        // imported above, which are still unverified.
        if (!hasLocalData) {
          lazy.console.debug(`${this.identifier} No previous data to restore`);
        }

        let localTrustworthy = false;
        if (hasLocalData) {
          try {
            localTrustworthy = await new Promise(verifySignatureLocalData);
          } catch (_) {
            // Verifying the data we had before syncing failed for another
            // reason than an invalid signature (eg. its cert chain could not
            // be fetched). Consider it untrustworthy, and fall back to the
            // dump or an empty database below.
          }
        }

        if (localTrustworthy) {
          // The data we had before syncing is valid: restore it, dropping the
          // unverified records imported above.
          lazy.console.debug(`${this.identifier} restore previous local data`);
          await this.db.importChanges(
            localMetadata,
            localTimestamp,
            localRecords,
            {
              clear: true, // clear before importing.
            }
          );
        } else if (!retry) {
          // Signature failed, clear local DB because it contains
          // bad data (local + remote changes).
          lazy.console.debug(`${this.identifier} clear local data`);
          await this.db.clear();
          // Local data was tampered, throw and it will retry from empty DB.
          lazy.console.error(`${this.identifier} local data was corrupted`);
          throw new CorruptedDataError(this.identifier);
        } else {
          // We retried already and have nothing trustworthy to restore.
          // Restore the dump if available (no-op if no dump)
          const imported = await this._importJSONDump();
          // _importJSONDump() only clears DB if dump is available,
          // therefore do it here!
          if (imported < 0) {
            await this.db.clear();
          }
        }
        throw e;
      }
    } else {
      lazy.console.warn(`${this.identifier} has signature disabled`);
    }

    // We build a sync result, based on remote changes.
    const syncResult = {
      current: localRecords,
      created: [],
      updated: [],
      deleted: [],
    };
    if (this.hasListeners("sync")) {
      // If we have some listeners for the "sync" event,
      // Compute the changes, comparing records before and after.
      syncResult.current = newRecords;
      const oldById = hasLocalData
        ? new Map(localRecords.map(e => [e.id, e]))
        : new Map();
      for (const r of newRecords) {
        const old = oldById.get(r.id);
        if (old) {
          oldById.delete(r.id);
          if (r.last_modified != old.last_modified) {
            syncResult.updated.push({ old, new: r });
          }
        } else {
          syncResult.created.push(r);
        }
      }
      syncResult.deleted = syncResult.deleted.concat(
        Array.from(oldById.values())
      );
      lazy.console.debug(
        `${this.identifier} ${syncResult.created.length} created. ${syncResult.updated.length} updated. ${syncResult.deleted.length} deleted.`
      );
    }

    return { syncResult, ageSeconds };
  }

  /**
   * Fetch information from changeset endpoint.
   *
   * @param expectedTimestamp cache busting value
   * @param since timestamp of last sync (optional)
   */
  async _fetchChangeset(expectedTimestamp, since) {
    const client = this.httpClient();
    const {
      headers,
      json: { metadata, timestamp: remoteTimestamp, changes: remoteRecords },
    } = await client.execute(
      {
        path: `/buckets/${this.bucketName}/collections/${this.collectionName}/changeset`,
      },
      {
        query: {
          _expected: expectedTimestamp,
          _since: since,
        },
        raw: true,
      }
    );
    const ageSeconds =
      (lazy.Utils.serverTimeMillis(headers) - remoteTimestamp) / 1000;
    return {
      remoteTimestamp,
      metadata,
      remoteRecords,
      ageSeconds,
    };
  }

  /**
   * Use the filter func to filter the lists of changes obtained from synchronization,
   * and return them along with the filtered list of local records.
   *
   * If the filtered lists of changes are all empty, we return null (and thus don't
   * bother listing local DB).
   *
   * @param {object}     syncResult       Synchronization result without filtering.
   *
   * @returns {Promise<object>} the filtered list of local records, plus the filtered
   *                            list of created, updated and deleted records.
   */
  async _filterSyncResult(syncResult) {
    // Handle the obtained records (ie. apply locally through events).
    // Build the event data list. It should be filtered (ie. by application target)
    const {
      current: allData,
      created: allCreated,
      updated: allUpdated,
      deleted: allDeleted,
    } = syncResult;
    const [created, deleted, updatedFiltered] = await Promise.all(
      [allCreated, allDeleted, allUpdated.map(e => e.new)].map(
        this._filterEntries.bind(this)
      )
    );
    // For updates, keep entries whose updated form matches the target.
    const updatedFilteredIds = new Set(updatedFiltered.map(e => e.id));
    const updated = allUpdated.filter(({ new: { id } }) =>
      updatedFilteredIds.has(id)
    );

    if (!created.length && !updated.length && !deleted.length) {
      return null;
    }
    // Read local collection of records (also filtered).
    const current = await this._filterEntries(allData);
    return { created, updated, deleted, current };
  }

  /**
   * Filter entries for which calls to the filter's `filterEntry` method
   * return null.
   *
   * @param {object[]} data
   * @returns {Promise<object[]>}
   */
  async _filterEntries(data) {
    if (!this.filterCreator) {
      return data;
    }
    const filter = await this.filterCreator(
      lazy.ClientEnvironmentBase,
      this.identifier
    );
    const results = [];
    for (const entry of data) {
      const filteredEntry = await filter.filterEntry(entry);
      if (filteredEntry) {
        results.push(filteredEntry);
      }
    }
    return results;
  }

  /**
   * Remove the fields from the specified record
   * that are not present on server.
   *
   * @param {object} record
   */
  _cleanLocalFields(record) {
    const keys = ["_status"].concat(this.localFields);
    const result = { ...record };
    for (const key of keys) {
      delete result[key];
    }
    return result;
  }
}
