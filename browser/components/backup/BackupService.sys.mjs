/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import * as DefaultBackupResources from "moz-src:///browser/components/backup/BackupResources.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { BackupResource } from "moz-src:///browser/components/backup/resources/BackupResource.sys.mjs";
import {
  MeasurementUtils,
  BYTES_IN_KILOBYTE,
  BYTES_IN_MEGABYTE,
  BYTES_IN_MEBIBYTE,
} from "moz-src:///browser/components/backup/MeasurementUtils.sys.mjs";

import {
  ERRORS,
  BACKUP_STEPS,
  RESTORE_STEPS,
  errorString,
} from "chrome://browser/content/backup/backup-constants.mjs";
import { BackupError } from "moz-src:///browser/components/backup/BackupError.mjs";

const BACKUP_DIR_PREF_NAME = "browser.backup.location";
const BACKUP_ERROR_CODE_PREF_NAME = "browser.backup.errorCode";
const SCHEDULED_BACKUPS_ENABLED_PREF_NAME = "browser.backup.scheduled.enabled";
const BACKUP_ARCHIVE_ENABLED_PREF_NAME = "browser.backup.archive.enabled";
const BACKUP_RESTORE_ENABLED_PREF_NAME = "browser.backup.restore.enabled";
const IDLE_THRESHOLD_SECONDS_PREF_NAME =
  "browser.backup.scheduled.idle-threshold-seconds";
const MINIMUM_TIME_BETWEEN_BACKUPS_SECONDS_PREF_NAME =
  "browser.backup.scheduled.minimum-time-between-backups-seconds";
const LAST_BACKUP_TIMESTAMP_PREF_NAME =
  "browser.backup.scheduled.last-backup-timestamp";
const LAST_BACKUP_FILE_NAME_PREF_NAME =
  "browser.backup.scheduled.last-backup-file";
const BACKUP_RETRY_LIMIT_PREF_NAME = "browser.backup.backup-retry-limit";
const DISABLED_ON_IDLE_RETRY_PREF_NAME =
  "browser.backup.disabled-on-idle-backup-retry";
const BACKUP_DEBUG_INFO_PREF_NAME = "browser.backup.backup-debug-info";
const MAXIMUM_NUMBER_OF_UNREMOVABLE_STAGING_ITEMS_PREF_NAME =
  "browser.backup.max-num-unremovable-staging-items";
const CREATED_MANAGED_PROFILES_PREF_NAME = "browser.profiles.created";
const RESTORED_BACKUP_METADATA_PREF_NAME =
  "browser.backup.restored-backup-metadata";
const SANITIZE_ON_SHUTDOWN_PREF_NAME = "privacy.sanitize.sanitizeOnShutdown";
const BACKUP_ENABLED_ON_PROFILES_PREF_NAME =
  "browser.backup.enabled_on.profiles";
const SQLITE_ENCRYPTION_ENABLED_PREF_NAME =
  "security.storage.encryption.sqlite.enabled";

const SCHEMAS = Object.freeze({
  BACKUP_MANIFEST: 1,
  ARCHIVE_JSON_BLOCK: 2,
});

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "BackupService",
    maxLogLevel: Services.prefs.getBoolPref("browser.backup.log", false)
      ? "Debug"
      : "Warn",
  });
});

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  ArchiveDecryptor:
    "moz-src:///browser/components/backup/ArchiveEncryption.sys.mjs",
  ArchiveEncryptionState:
    "moz-src:///browser/components/backup/ArchiveEncryptionState.sys.mjs",
  ArchiveUtils: "moz-src:///browser/components/backup/ArchiveUtils.sys.mjs",
  BasePromiseWorker: "resource://gre/modules/PromiseWorker.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "ZipWriter", () =>
  Components.Constructor("@mozilla.org/zipwriter;1", "nsIZipWriter", "open")
);
ChromeUtils.defineLazyGetter(lazy, "ZipReader", () =>
  Components.Constructor(
    "@mozilla.org/libjar/zip-reader;1",
    "nsIZipReader",
    "open"
  )
);
ChromeUtils.defineLazyGetter(lazy, "nsLocalFile", () =>
  Components.Constructor("@mozilla.org/file/local;1", "nsIFile", "initWithPath")
);

ChromeUtils.defineLazyGetter(lazy, "BinaryInputStream", () =>
  Components.Constructor(
    "@mozilla.org/binaryinputstream;1",
    "nsIBinaryInputStream",
    "setInputStream"
  )
);

ChromeUtils.defineLazyGetter(lazy, "gFluentStrings", function () {
  return new Localization(
    ["branding/brand.ftl", "browser/backupSettings.ftl"],
    true
  );
});

ChromeUtils.defineLazyGetter(lazy, "gDOMLocalization", function () {
  return new DOMLocalization([
    "branding/brand.ftl",
    "browser/backupSettings.ftl",
  ]);
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "scheduledBackupsPref",
  SCHEDULED_BACKUPS_ENABLED_PREF_NAME,
  false,
  function onUpdateScheduledBackups(_pref, _prevVal, newVal) {
    let bs = BackupService.get();
    if (bs) {
      bs.onUpdateScheduledBackups(newVal);
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "backupDirPref",
  BACKUP_DIR_PREF_NAME,
  /**
   * To avoid disk access upon startup, do not set DEFAULT_PARENT_DIR_PATH
   * as a fallback value here. Let registered widgets prompt BackupService
   * to update the parentDirPath.
   *
   * @see BackupService.state
   * @see DEFAULT_PARENT_DIR_PATH
   * @see setParentDirPath
   */
  null,
  async function onUpdateLocationDirPath(_pref, _prevVal, newVal) {
    let bs;
    try {
      bs = BackupService.get();
    } catch (e) {
      // This can throw if the BackupService hasn't initialized yet, which
      // is a case we're okay to ignore.
    }
    if (bs) {
      await bs.onUpdateLocationDirPath(newVal);
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minimumTimeBetweenBackupsSeconds",
  MINIMUM_TIME_BETWEEN_BACKUPS_SECONDS_PREF_NAME,
  86400 /* 1 day */
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "backupRetryLimit",
  BACKUP_RETRY_LIMIT_PREF_NAME,
  100
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "isRetryDisabledOnIdle",
  DISABLED_ON_IDLE_RETRY_PREF_NAME,
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "maximumNumberOfUnremovableStagingItems",
  MAXIMUM_NUMBER_OF_UNREMOVABLE_STAGING_ITEMS_PREF_NAME,
  5
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enabledOnProfilesPref",
  BACKUP_ENABLED_ON_PROFILES_PREF_NAME,
  "[]",
  null,
  function transform(rawValue) {
    let parsed;
    try {
      parsed = JSON.parse(rawValue);
    } catch {
      return [];
    }

    if (Array.isArray(parsed)) {
      return parsed;
    }

    // Migrate from legacy object format {profileId: true, ...} to array.
    let profilesArray = Object.keys(parsed);
    Services.prefs.setStringPref(
      BACKUP_ENABLED_ON_PROFILES_PREF_NAME,
      JSON.stringify(profilesArray)
    );
    return profilesArray;
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "backupErrorCode",
  BACKUP_ERROR_CODE_PREF_NAME,
  0,
  function onUpdateBackupErrorCode(_pref, _prevVal, newVal) {
    let bs = BackupService.get();
    if (bs) {
      bs.onUpdateBackupErrorCode(newVal);
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "lastBackupFileName",
  LAST_BACKUP_FILE_NAME_PREF_NAME,
  "",
  function onUpdateLastBackupFileName(_pref, _prevVal, newVal) {
    let bs;
    try {
      bs = BackupService.get();
    } catch (e) {
      // This can throw if the BackupService hasn't initialized yet, which
      // is a case we're okay to ignore.
    }
    if (bs) {
      bs.onUpdateLastBackupFileName(newVal);
    }
  }
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "idleService",
  "@mozilla.org/widget/useridleservice;1",
  Ci.nsIUserIdleService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "nativeOSKeyStore",
  "@mozilla.org/security/oskeystore;1",
  Ci.nsIOSKeyStore
);

/**
 * A class that wraps a multipart/mixed stream converter instance, and streams
 * in the binary part of a single-file archive (which should be at the second
 * index of the attachments) as a ReadableStream.
 *
 * The bytes that are read in are text decoded, but are not guaranteed to
 * represent a "full chunk" of base64 data. Consumers should ensure to buffer
 * the strings emitted by this stream, and to search for `\n` characters, which
 * indicate the end of a (potentially encrypted and) base64 encoded block.
 */
class BinaryReadableStream {
  #channel = null;

  /**
   * Constructs a BinaryReadableStream.
   *
   * @param {nsIChannel} channel
   *   The channel through which to begin the flow of bytes from the
   *   inputStream
   */
  constructor(channel) {
    this.#channel = channel;
  }

  /**
   * Implements `start` from the `underlyingSource` of a ReadableStream
   *
   * @param {ReadableStreamDefaultController} controller
   *   The controller for the ReadableStream to feed strings into.
   */
  start(controller) {
    let streamConv = Cc["@mozilla.org/streamConverters;1"].getService(
      Ci.nsIStreamConverterService
    );

    let textDecoder = new TextDecoder();

    // The attachment index that should contain the binary data.
    const EXPECTED_CONTENT_TYPE = "application/octet-stream";

    // This is fairly clumsy, but by using an object nsIStreamListener like
    // this, I can keep from stashing the `controller` somewhere, as it's
    // available in the closure.
    let multipartListenerForBinary = {
      /**
       * True once we've found an attachment matching our EXPECTED_CONTENT_TYPE.
       * Once this is true, bytes flowing into onDataAvailable will be
       * enqueued through the controller.
       *
       * @type {boolean}
       */
      _enabled: false,

      /**
       * True once onStopRequest has been called once the listener is enabled.
       * After this, the listener will not attempt to read any data passed
       * to it through onDataAvailable.
       *
       * @type {boolean}
       */
      _done: false,

      QueryInterface: ChromeUtils.generateQI([
        "nsIStreamListener",
        "nsIRequestObserver",
        "nsIMultiPartChannelListener",
      ]),

      /**
       * Called when we begin to load an attachment from the MIME message.
       *
       * @param {nsIRequest} request
       *   The request corresponding to the source of the data.
       */
      onStartRequest(request) {
        if (!(request instanceof Ci.nsIChannel)) {
          throw Components.Exception(
            "onStartRequest expected an nsIChannel request",
            Cr.NS_ERROR_UNEXPECTED
          );
        }
        this._enabled = request.contentType == EXPECTED_CONTENT_TYPE;
      },

      /**
       * Called when data is flowing in for an attachment.
       *
       * @param {nsIRequest} request
       *   The request corresponding to the source of the data.
       * @param {nsIInputStream} stream
       *   The input stream containing the data chunk.
       * @param {number} offset
       *   The number of bytes that were sent in previous onDataAvailable calls
       *   for this request. In other words, the sum of all previous count
       *   parameters.
       * @param {number} count
       *   The number of bytes available in the stream
       */
      onDataAvailable(request, stream, offset, count) {
        if (this._done) {
          // No need to load anything else - abort reading in more
          // attachments.
          throw Components.Exception(
            "Got binary block - cancelling loading the multipart stream.",
            Cr.NS_BINDING_ABORTED
          );
        }
        if (!this._enabled) {
          // We don't care about this data, just move on.
          return;
        }

        let binStream = new lazy.BinaryInputStream(stream);
        let bytes = new Uint8Array(count);
        binStream.readArrayBuffer(count, bytes.buffer);
        let string = textDecoder.decode(bytes);
        controller.enqueue(string);
      },

      /**
       * Called when the load of an attachment finishes.
       */
      onStopRequest() {
        if (this._enabled && !this._done) {
          this._enabled = false;
          this._done = true;

          controller.close();
        }
      },

      onAfterLastPart() {
        if (!this._done) {
          // We finished reading the parts before we found the binary block,
          // so the binary block is missing.
          controller.error(
            new BackupError(
              "Could not find binary block.",
              ERRORS.CORRUPTED_ARCHIVE
            )
          );
        }
      },
    };

    let conv = streamConv.asyncConvertData(
      "multipart/mixed",
      "*/*",
      multipartListenerForBinary,
      null
    );

    this.#channel.asyncOpen(conv);
  }
}

/**
 * A TransformStream class that takes in chunks of base64 encoded data,
 * decodes (and eventually, decrypts) them before passing the resulting
 * bytes along to the next step in the pipe.
 *
 * The BinaryReadableStream feeds strings into this TransformStream, but the
 * buffering of these streams means that we cannot be certain that the string
 * that was passed is the entirety of a base64 encoded block. ArchiveWorker
 * puts every block on its own line, meaning that we must simply look for
 * newlines to indicate when a break between full blocks is, and buffer chunks
 * until we see those breaks - only decoding once we have a full block.
 */
export class DecoderDecryptorTransformer {
  #buffer = "";
  #decryptor = null;

  /**
   * Constructs the DecoderDecryptorTransformer.
   *
   * @param {ArchiveDecryptor|null} decryptor
   *   An initialized ArchiveDecryptor, if this stream of bytes is presumed to
   *   be encrypted.
   */
  constructor(decryptor) {
    this.#decryptor = decryptor;
  }

  /**
   * Consumes a single chunk of a base64 encoded string sent by
   * BinaryReadableStream.
   *
   * @param {string} chunkPart
   *   A part of a chunk of a base64 encoded string sent by
   *   BinaryReadableStream.
   * @param {TransformStreamDefaultController} controller
   *   The controller to send decoded bytes to.
   * @returns {Promise<undefined>}
   */
  async transform(chunkPart, controller) {
    // A small optimization, but considering the size of these strings, it's
    // likely worth it.
    if (this.#buffer) {
      this.#buffer += chunkPart;
    } else {
      this.#buffer = chunkPart;
    }

    // If the compressed archive was large enough, then it got split up over
    // several chunks. In that case, each chunk is separated by a newline. We
    // also filter out any extraneous newlines that might have been included
    // at the end.
    let chunks = this.#buffer.split("\n").filter(chunk => chunk != "");

    this.#buffer = chunks.pop();
    // If there were any remaining parts that we split out from the buffer,
    // they must constitute full blocks that we can decode.
    for (let chunk of chunks) {
      await this.#processChunk(controller, chunk);
    }
  }

  /**
   * Called once BinaryReadableStream signals that it has sent all of its
   * strings, in which case we know that whatever is in the buffer should be
   * a valid block.
   *
   * @param {TransformStreamDefaultController} controller
   *   The controller to send decoded bytes to.
   * @returns {Promise<undefined>}
   */
  async flush(controller) {
    await this.#processChunk(controller, this.#buffer, true);
    this.#buffer = "";
  }

  /**
   * Decodes (and potentially decrypts) a valid base64 encoded chunk into a
   * Uint8Array and sends it to the next step in the pipe.
   *
   * @param {TransformStreamDefaultController} controller
   *   The controller to send decoded bytes to.
   * @param {string} chunk
   *   The base64 encoded string to decode and potentially decrypt.
   * @param {boolean} [isLastChunk=false]
   *   True if this is the last chunk to be processed.
   * @returns {Promise<undefined>}
   */
  async #processChunk(controller, chunk, isLastChunk = false) {
    try {
      let bytes = lazy.ArchiveUtils.stringToArray(chunk);

      if (this.#decryptor) {
        let plaintextBytes = await this.#decryptor.decrypt(bytes, isLastChunk);
        controller.enqueue(plaintextBytes);
      } else {
        controller.enqueue(bytes);
      }
    } catch (e) {
      throw e instanceof BackupError
        ? e
        : new BackupError("Corrupted archive", ERRORS.CORRUPTED_ARCHIVE);
    }
  }
}

/**
 * A class that lets us construct a WritableStream that writes bytes to a file
 * on disk somewhere.
 */
export class FileWriterStream {
  /**
   * @type {string}
   */
  #destPath = null;

  /**
   * @type {nsIOutputStream}
   */
  #outStream = null;

  /**
   * @type {nsIBinaryOutputStream}
   */
  #binStream = null;

  /**
   * @type {ArchiveDecryptor}
   */
  #decryptor = null;

  /**
   * Constructor for FileWriterStream.
   *
   * @param {string} destPath
   *   The path to write the incoming bytes to.
   * @param {ArchiveDecryptor|null} decryptor
   *   An initialized ArchiveDecryptor, if this stream of bytes is presumed to
   *   be encrypted.
   */
  constructor(destPath, decryptor) {
    this.#destPath = destPath;
    this.#decryptor = decryptor;
  }

  /**
   * Called once the first set of bytes comes in from the
   * DecoderDecryptorTransformer. This creates the file, and sets up the
   * underlying nsIOutputStream mechanisms to let us write bytes to the file.
   */
  async start() {
    let extractionDestFile = await IOUtils.getFile(this.#destPath);
    this.#outStream =
      lazy.FileUtils.openSafeFileOutputStream(extractionDestFile);
    this.#binStream = Cc["@mozilla.org/binaryoutputstream;1"].createInstance(
      Ci.nsIBinaryOutputStream
    );
    this.#binStream.setOutputStream(this.#outStream);
  }

  /**
   * Writes bytes to the destination on the file system.
   *
   * @param {Uint8Array} chunk
   *   The bytes to stream to the destination file.
   */
  write(chunk) {
    this.#binStream.writeByteArray(chunk);
  }

  /**
   * Called once the stream of bytes finishes flowing in and closes the stream.
   *
   * @param {WritableStreamDefaultController} controller
   *   The controller for the WritableStream.
   */
  close(controller) {
    lazy.FileUtils.closeSafeFileOutputStream(this.#outStream);
    if (this.#decryptor && !this.#decryptor.isDone()) {
      lazy.logConsole.error(
        "Decryptor was not done when the stream was closed."
      );
      controller.error(
        new BackupError("Corrupted archive.", ERRORS.DECRYPTION_FAILED)
      );
    }
  }

  /**
   * Called if something went wrong while decoding / decrypting the stream of
   * bytes. This destroys any bytes that may have been decoded / decrypted
   * prior to the error.
   *
   * @param {string} reason
   *   The reported reason for aborting the decoding / decrpytion.
   */
  async abort(reason) {
    lazy.logConsole.error(`Writing to ${this.#destPath} failed: `, reason);
    lazy.FileUtils.closeSafeFileOutputStream(this.#outStream);
    await IOUtils.remove(this.#destPath, {
      ignoreAbsent: true,
      retryReadonly: true,
    });
  }
}

/**
 * The BackupService class orchestrates the scheduling and creation of profile
 * backups. It also does most of the heavy lifting for the restoration of a
 * profile backup.
 */
export class BackupService extends EventTarget {
  /**
   * The BackupService singleton instance.
   *
   * @static
   * @type {BackupService|null}
   */
  static #instance = null;

  /**
   * Map of instantiated BackupResource classes.
   *
   * @type {Map<string, BackupResource>}
   */
  #resources = new Map();

  /**
   * The name of the backup folder. Should be localized.
   *
   * @see BACKUP_DIR_NAME
   */
  static #backupFolderName = "Restore Firefox";

  /**
   * The name of the backup archive file. Should be localized.
   *
   * @see BACKUP_FILE_NAME
   */
  static #backupFileName = null;

  /**
   * Number of retries that have occured in this session on error
   */
  static #errorRetries = 0;

  /**
   * Time to wait (in seconds) until the next backup attempt.
   *
   * This uses exponential backoff based on the number of consecutive
   * failed backup attempts since the last successful backup.
   *
   * Backoff formula:
   *
   *   ``2^(retryCount) * 60``
   *
   * Example:
   *
   *   If 2 backup attempts have failed since the last successful backup,
   *   the next attempt will occur after:
   *
   *   ``2^2 * 60 = 240 seconds (4 minutes)``
   *
   * This differs from minimumTimeBetweenBackupsSeconds, which is used to determine
   * the time between successful backups.
   */
  static backoffSeconds = () => Math.pow(2, BackupService.#errorRetries) * 60;

  /**
   * @typedef {object} EnabledStatus
   * @property {boolean} enabled
   *   True if the feature is enabled.
   * @property {string} [reason]
   *   Reason the feature is disabled if `enabled` is false.
   */

  /**
   * Context for whether creating a backup archive is enabled.
   *
   * @type {EnabledStatus}
   */
  get archiveEnabledStatus() {
    // Backup is unsupported while SQLite at-rest encryption is on: staged
    // database copies are written as ciphertext whose keys live only in this
    // profile's lockstore, so the archive cannot be restored elsewhere.
    if (
      Services.prefs.getBoolPref(SQLITE_ENCRYPTION_ENABLED_PREF_NAME, false)
    ) {
      return {
        enabled: false,
        reason: "Archiving a profile disabled while SQLite encryption is on.",
        internalReason: "sqlite-encryption",
      };
    }

    // Check if disabled by Nimbus killswitch.
    const archiveKillswitchTriggered =
      lazy.NimbusFeatures.backupService.getVariable("archiveKillswitch");

    // Only disable feature if archiveKillswitch is true.
    if (archiveKillswitchTriggered) {
      return {
        enabled: false,
        reason: "Archiving a profile disabled remotely.",
        internalReason: "nimbus",
      };
    }

    if (!Services.prefs.getBoolPref(BACKUP_ARCHIVE_ENABLED_PREF_NAME)) {
      if (Services.prefs.prefIsLocked(BACKUP_ARCHIVE_ENABLED_PREF_NAME)) {
        // If it's locked, assume it was set by an enterprise policy.
        return {
          enabled: false,
          reason: "Archiving a profile disabled by policy.",
          internalReason: "policy",
        };
      }

      return {
        enabled: false,
        reason: "Archiving a profile disabled by user pref.",
        internalReason: "pref",
      };
    }

    return { enabled: true };
  }

  /**
   * Context for whether restore from backup is enabled.
   *
   * @type {EnabledStatus}
   */
  get restoreEnabledStatus() {
    // Restoring into an instance with SQLite at-rest encryption on would copy
    // plaintext database files that obfsvfs then rejects fail-closed, leaving a
    // broken profile, so restore is disabled while encryption is on.
    if (
      Services.prefs.getBoolPref(SQLITE_ENCRYPTION_ENABLED_PREF_NAME, false)
    ) {
      return {
        enabled: false,
        reason: "Restoring a profile disabled while SQLite encryption is on.",
        internalReason: "sqlite-encryption",
      };
    }

    // Check if disabled by Nimbus killswitch.
    const restoreKillswitchTriggered =
      lazy.NimbusFeatures.backupService.getVariable("restoreKillswitch");

    if (restoreKillswitchTriggered) {
      return {
        enabled: false,
        reason: "Restore from backup disabled remotely.",
        internalReason: "nimbus",
      };
    }

    if (!Services.prefs.getBoolPref(BACKUP_RESTORE_ENABLED_PREF_NAME)) {
      if (Services.prefs.prefIsLocked(BACKUP_RESTORE_ENABLED_PREF_NAME)) {
        // If it's locked, assume it was set by an enterprise policy.
        return {
          enabled: false,
          reason: "Restoring a profile disabled by policy.",
          internalReason: "policy",
        };
      }

      return {
        enabled: false,
        reason: "Restoring a profile disabled by user pref.",
        internalReason: "pref",
      };
    }

    return { enabled: true };
  }

  /**
   * Set to true if a backup is currently in progress. Causes stateUpdate()
   * to be called.
   *
   * @see BackupService.stateUpdate()
   * @param {boolean} val
   *   True if a backup is in progress.
   */
  set #backupInProgress(val) {
    if (this.#_state.backupInProgress != val) {
      this.#_state.backupInProgress = val;
      this.stateUpdate();
    }
  }

  /**
   * True if a backup is currently in progress.
   *
   * @type {boolean}
   */
  get #backupInProgress() {
    return this.#_state.backupInProgress;
  }

  /**
   * Dispatches an event to let listeners know that the BackupService state
   * object has been updated.
   */
  stateUpdate() {
    this.dispatchEvent(new CustomEvent("BackupService:StateUpdate"));
  }

  /**
   * Sets the recovery error code and updates the state.
   *
   * @param {number} errorCode - The error code to set
   */
  setRecoveryError(errorCode) {
    this.#_state.recoveryErrorCode = errorCode;
    this.stateUpdate();
  }

  /**
   * Sets the persisted options between screens for embedded components.
   * This is specifically used in the Spotlight onboarding experience.
   *
   * This data is flushed upon creating a backup or exiting the backup flow.
   *
   * @param {object} data - data to persist between screens.
   */
  setEmbeddedComponentPersistentData(data) {
    this.#_state.embeddedComponentPersistentData = { ...data };
    this.stateUpdate();
  }

  /**
   * An object holding the current state of the BackupService instance, for
   * the purposes of representing it in the user interface. Ideally, this would
   * be named #state instead of #_state, but sphinx-js seems to be fairly
   * unhappy with that coupled with the ``state`` getter.
   *
   * @type {object}
   */
  #_state = {
    backupDirPath: lazy.backupDirPref,
    defaultParent: {},
    backupFileToRestore: null,
    backupFileInfo: null,
    backupInProgress: false,
    scheduledBackupsEnabled: lazy.scheduledBackupsPref,
    encryptionEnabled: false,
    /**
     * Number of seconds since UNIX epoch
     *
     * @type {number?}
     */
    lastBackupDate: null,
    lastBackupFileName: lazy.lastBackupFileName,
    supportBaseLink: Services.urlFormatter.formatURLPref("app.support.baseURL"),
    recoveryInProgress: false,
    /**
     * Every file we load successfully is going to get a restore ID which is
     * basically the identifier for that profile restore event. If we actually
     * do restore it, this ID will end up being propagated into the restored
     * file and used to correlate this restore event with the profile that was
     * restored.
     */
    restoreID: null,
    /** Utilized by the spotlight to persist information between screens */
    embeddedComponentPersistentData: {},
    recoveryErrorCode: ERRORS.NONE,
    backupErrorCode: lazy.backupErrorCode,
    selectableProfilesAllowed: lazy.SelectableProfileService.isEnabled,
  };

  /**
   * A Promise that will resolve once the postRecovery steps are done. It will
   * also resolve if postRecovery steps didn't need to run.
   *
   * @see BackupService.checkForPostRecovery()
   * @type {Promise<undefined>}
   */
  #postRecoveryPromise;

  /**
   * The resolving function for #postRecoveryPromise, which should be called
   * by checkForPostRecovery() before exiting.
   *
   * @type {Function}
   */
  #postRecoveryResolver;

  /**
   * The currently used ArchiveEncryptionState. Callers should use
   * loadEncryptionState() instead, to ensure that any pre-serialized
   * encryption state has been read in and deserialized.
   *
   * This member can be in 3 states:
   *
   * 1. undefined - no attempt has been made to load encryption state from
   *    disk yet.
   * 2. null - encryption is not enabled.
   * 3. ArchiveEncryptionState - encryption is enabled.
   *
   * @see BackupService.loadEncryptionState()
   * @type {ArchiveEncryptionState|null|undefined}
   */
  #encState = undefined;

  /**
   * The PlacesObserver instance used to monitor the Places database for
   * history and bookmark removals to determine if backups should be
   * regenerated.
   *
   * @type {PlacesObserver|null}
   */
  #placesObserver = null;

  /**
   * The AbortController used to abort any queued requests to create or delete
   * backups that might be waiting on the WRITE_BACKUP_LOCK_NAME lock.
   *
   * @type {AbortController}
   */
  #backupWriteAbortController = null;

  /**
   * A DeferredTask that will cause the last known backup to be deleted, and
   * a new backup to be created.
   *
   * See BackupService.#debounceRegeneration()
   *
   * @type {DeferredTask}
   */
  #regenerationDebouncer = null;

  /**
   * True if takeMeasurements has been called and various measurements related
   * to the BackupService have been taken.
   *
   * @type {boolean}
   */
  #takenMeasurements = false;

  /**
   * Stores whether backing up has been disabled at some point during this
   * session. If it has been, the archiveDisabledReason telemetry metric is set
   * on each backup. (It cannot be unset due to Glean limitations.)
   *
   * @type {boolean}
   */
  #wasArchivePreviouslyDisabled = false;

  /**
   * Stores whether restoring up has been disabled at some point during this
   * session. If it has been, the restoreDisabledReason telemetry metric is set
   * on each backup. (It cannot be unset due to Glean limitations.)
   *
   * @type {boolean}
   */
  #wasRestorePreviouslyDisabled = false;

  /**
   * Identifies the UI that triggered the most recent call to
   * setScheduledBackups(). Read once by onUpdateScheduledBackups() when the
   * pref change actually flips state (in either direction), at which point it
   * is reset to "unknown" so a subsequent direct pref flip does not inherit a
   * stale source.
   *
   * @type {string}
   */
  #scheduledBackupsToggleSource = "unknown";

  /**
   * Called when prefs or other conditions relevant to the status of the backup
   * service change. Unlike #observer, this does not wait for an idle tick.
   *
   * This callback doesn't take any parameters. It's here so it can be removed
   * by uninitStatusObservers, and also so its 'this' value remains accurate.
   * If null, the conditions are not currently being monitored.
   *
   * @type {Function?}
   */
  #statusPrefObserver = null;

  /**
   * Called when the SelectableProfileService state is updated. Stored as a
   * member so it can be unregistered from the SelectableProfileService by
   * uninitStatusObservers. If null, the conditions are not currently being
   * monitored.
   *
   * @type {Function?}
   */
  #profileServiceStateObserver = null;

  /**
   * The path of the default parent directory for saving backups.
   * The current default is the Documents directory.
   *
   * @returns {string} The path of the default parent directory
   */
  static get DEFAULT_PARENT_DIR_PATH() {
    return (
      BackupService.oneDriveFolderPath?.path ||
      BackupService.docsDirFolderPath?.path ||
      ""
    );
  }

  /**
   * The localized name for the user's backup folder.
   *
   * @returns {string} The localized backup folder name
   */
  static get BACKUP_DIR_NAME() {
    if (!BackupService.#backupFolderName) {
      BackupService.#backupFolderName = lazy.DownloadPaths.sanitize(
        lazy.gFluentStrings.formatValueSync("backup-folder-name")
      );
    }
    return BackupService.#backupFolderName;
  }

  /**
   * The setting name which has the user's backup path as value.
   *
   * @returns {string} the name of a setting.
   */
  static get BACKUP_DIR_PREF_NAME() {
    return BACKUP_DIR_PREF_NAME;
  }

  /**
   * The value of BACKUP_DIR_NAME can be configured through a desktop.ini file,
   * enabling users to see a custom display name while the actual folder has a
   * different, disk-based name. This approach allows for more efficient
   * automatic detection of existing backups, as it avoids the need to iterate
   * over language versions on the disk.
   *
   * @returns {string} the backup folder's descriptive name (translation)
   */
  static get BACKUP_DIR_TRANSLATION() {
    let folderDesc = lazy.gFluentStrings.formatValueSync("backup-folder-name");
    folderDesc =
      folderDesc != "" ? folderDesc : BackupService.#backupFolderName;
    return folderDesc;
  }

  /**
   * The localized name for the user's backup archive file. This will have
   * `.html` appended to it before writing the archive file.
   *
   * @returns {string} The localized backup file name
   */
  static get BACKUP_FILE_NAME() {
    if (!BackupService.#backupFileName) {
      BackupService.#backupFileName = lazy.DownloadPaths.sanitize(
        lazy.gFluentStrings.formatValueSync("backup-file-name")
      );
    }
    return BackupService.#backupFileName;
  }

  /**
   * The name of the folder within the profile folder where this service reads
   * and writes state to.
   *
   * @type {string}
   */
  static get PROFILE_FOLDER_NAME() {
    return "backups";
  }

  /**
   * The name of the folder within the PROFILE_FOLDER_NAME where the staging
   * folder / prior backups will be stored.
   *
   * @type {string}
   */
  static get SNAPSHOTS_FOLDER_NAME() {
    return "snapshots";
  }

  /**
   * The name of the backup manifest file.
   *
   * @type {string}
   */
  static get MANIFEST_FILE_NAME() {
    return "backup-manifest.json";
  }

  /**
   * A promise that resolves to the schema for the backup manifest that this
   * BackupService uses when creating a backup. This should be accessed via
   * the `MANIFEST_SCHEMA` static getter.
   *
   * @type {Promise<object>}
   */
  static #manifestSchemaPromise = null;

  /**
   * The current schema version of the backup manifest that this BackupService
   * uses when creating a backup.
   *
   * @type {Promise<object>}
   */
  static get MANIFEST_SCHEMA() {
    if (!BackupService.#manifestSchemaPromise) {
      BackupService.#manifestSchemaPromise = BackupService.getSchemaForVersion(
        SCHEMAS.BACKUP_MANIFEST,
        lazy.ArchiveUtils.SCHEMA_VERSION
      );
    }

    return BackupService.#manifestSchemaPromise;
  }

  /**
   * The name of the post recovery file written into the newly created profile
   * directory just after a profile is recovered from a backup.
   *
   * @type {string}
   */
  static get POST_RECOVERY_FILE_NAME() {
    return "post-recovery.json";
  }

  /**
   * The name of the serialized ArchiveEncryptionState that is written to disk
   * if encryption is enabled.
   *
   * @type {string}
   */
  static get ARCHIVE_ENCRYPTION_STATE_FILE() {
    return "enc-state.json";
  }

  /**
   * Returns the SCHEMAS constants, which is a key/value store of constants.
   *
   * @type {object}
   */
  static get SCHEMAS() {
    return SCHEMAS;
  }

  /**
   * Returns the filename used for the intermediary compressed ZIP file that
   * is extracted from archives during recovery.
   *
   * @type {string}
   */
  static get RECOVERY_ZIP_FILE_NAME() {
    return "recovery.zip";
  }

  /**
   * Prefs that should be monitored. When one of these prefs changes, the
   * 'backup-service-status-changed' observers are notified and telemetry
   * updates.
   *
   * @type {string[]}
   */
  static get STATUS_OBSERVER_PREFS() {
    return [
      BACKUP_ARCHIVE_ENABLED_PREF_NAME,
      BACKUP_RESTORE_ENABLED_PREF_NAME,
      SANITIZE_ON_SHUTDOWN_PREF_NAME,
      CREATED_MANAGED_PROFILES_PREF_NAME,
    ];
  }

  /**
   * Returns the schema for the schemaType for a given version.
   *
   * @param {number} schemaType
   *   One of the constants from SCHEMAS.
   * @param {number} version
   *   The version of the schema to return.
   * @returns {Promise<object>}
   */
  static async getSchemaForVersion(schemaType, version) {
    let schemaURL;

    if (schemaType == SCHEMAS.BACKUP_MANIFEST) {
      schemaURL = `chrome://browser/content/backup/BackupManifest.${version}.schema.json`;
    } else if (schemaType == SCHEMAS.ARCHIVE_JSON_BLOCK) {
      schemaURL = `chrome://browser/content/backup/ArchiveJSONBlock.${version}.schema.json`;
    } else {
      throw new BackupError(
        `Did not recognize SCHEMAS constant: ${schemaType}`,
        ERRORS.UNKNOWN
      );
    }

    let response = await fetch(schemaURL);
    return response.json();
  }

  /**
   * The level of Zip compression to use on the zipped staging folder.
   *
   * @type {number}
   */
  static get COMPRESSION_LEVEL() {
    return Ci.nsIZipWriter.COMPRESSION_BEST;
  }

  /**
   * Returns the chrome:// URI string for the template that should be used to
   * construct the single-file archive.
   *
   * @type {string}
   */
  static get ARCHIVE_TEMPLATE() {
    return "chrome://browser/content/backup/archive.template.html";
  }

  /**
   * The native OSKeyStore label used for the temporary recovery store. The
   * temporary recovery store is initialized with the original OSKeyStore
   * secret that was included in an encrypted backup, and then used by any
   * BackupResource's that need to decrypt / re-encrypt OSKeyStore secrets for
   * the current device.
   *
   * @type {string}
   */
  static get RECOVERY_OSKEYSTORE_LABEL() {
    return AppConstants.MOZ_APP_BASENAME + " Backup Recovery Storage";
  }

  /**
   * The name of the exclusive Web Lock that will be requested and held when
   * creating or deleting a backup.
   *
   * @type {string}
   */
  static get WRITE_BACKUP_LOCK_NAME() {
    return "write-backup";
  }

  /**
   * The amount of time (in milliseconds) to wait for our backup regeneration
   * debouncer to kick off a regeneration.
   *
   * @type {number}
   */
  static get REGENERATION_DEBOUNCE_RATE_MS() {
    return 10000;
  }

  /**
   * The user's personal OneDrive folder, or null if none exists.
   *
   * @returns {nsIFile|null} The OneDrive folder or null
   */
  static get oneDriveFolderPath() {
    try {
      let oneDriveDir = Services.dirsvc.get("OneDrPD", Ci.nsIFile);
      // This check should be redundant -- the OneDrive folder should exist.
      return oneDriveDir.exists() ? oneDriveDir : null;
    } catch {
      // Ignore exceptions.  The OneDrive folder not existing is an exception.
    }
    return null;
  }

  /**
   * Gets the user's Documents folder.
   * If it doesn't exist, return null.
   *
   * @returns {nsIFile|null} The Documents folder or null
   */
  static get docsDirFolderPath() {
    try {
      return Services.dirsvc.get("Docs", Ci.nsIFile);
    } catch (e) {
      lazy.logConsole.warn(
        "There was an error while trying to get the Document's directory",
        e
      );
    }
    return null;
  }

  /**
   * Probes whether read access to a backup parent directory is available.
   *
   * @param {string} [path=BackupService.DEFAULT_PARENT_DIR_PATH]
   *   The directory path to probe.
   * @returns {Promise<boolean>}
   *   Resolves to true if the directory contents could be listed,
   *   false otherwise.
   */
  async probeDefaultDirAccess(path = BackupService.DEFAULT_PARENT_DIR_PATH) {
    if (!path) {
      return false;
    }
    try {
      await IOUtils.getChildren(path);
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Returns a reference to a BackupService singleton. If this is the first time
   * that this getter is accessed, this causes the BackupService singleton to be
   * instantiated.
   *
   * @static
   * @param {object} BackupResources
   *   Optional object containing BackupResource classes to initialize the instance with.
   * @returns {BackupService}
   *   The BackupService singleton instance.
   */
  static init(BackupResources = DefaultBackupResources) {
    if (this.#instance) {
      return this.#instance;
    }

    // If there is unsent restore telemetry, send it now.
    GleanPings.profileRestore.submit();

    this.#instance = new BackupService(BackupResources);

    this.#instance.checkForPostRecovery();
    this.#instance.initBackupScheduler();
    this.#instance.initStatusObservers();
    return this.#instance;
  }

  /**
   * Clears the BackupService singleton instance.
   * This should only be used in tests.
   *
   * @static
   */
  static uninit() {
    if (this.#instance) {
      lazy.logConsole.debug("Uninitting the BackupService");

      this.#instance.uninitBackupScheduler();
      this.#instance.uninitStatusObservers();
      this.#instance = null;
    }
  }

  /**
   * Returns a reference to the BackupService singleton. If the singleton has
   * not been initialized, an error is thrown.
   *
   * @static
   * @returns {BackupService}
   */
  static get() {
    if (!this.#instance) {
      throw new BackupError(
        "BackupService not initialized",
        ERRORS.UNINITIALIZED
      );
    }
    return this.#instance;
  }

  /**
   * Create a BackupService instance.
   *
   * @param {object} [backupResources=DefaultBackupResources]
   *   Object containing BackupResource classes to associate with this service.
   */
  constructor(backupResources = DefaultBackupResources) {
    super();
    lazy.logConsole.debug("Instantiated");

    for (const resourceName in backupResources) {
      let resource = backupResources[resourceName];
      this.#resources.set(resource.key, resource);
    }

    let { promise, resolve } = Promise.withResolvers();
    this.#postRecoveryPromise = promise;
    this.#postRecoveryResolver = resolve;
    this.#backupWriteAbortController = new AbortController();
    this.#regenerationDebouncer = new lazy.DeferredTask(async () => {
      if (
        !this.#backupWriteAbortController.signal.aborted &&
        this.archiveEnabledStatus.enabled
      ) {
        await this.createBackupOnIdleDispatch({
          reason: "user deleted some data",
        });
      }
    }, BackupService.REGENERATION_DEBOUNCE_RATE_MS);
    this.#postRecoveryPromise.then(() => {
      this.#setRestoredProfileDataMetric();
    });

    this.#lastSeenArchiveStatus = this.archiveEnabledStatus;
    this.#lastSeenRestoreStatus = this.restoreEnabledStatus;
  }

  // Remembering status allows us to notify observers when the status changes
  #lastSeenArchiveStatus = false;
  #lastSeenRestoreStatus = false;

  /**
   * Returns a reference to a Promise that will resolve with undefined once
   * postRecovery steps have had a chance to run. This will also be resolved
   * with undefined if no postRecovery steps needed to be run.
   *
   * @see BackupService.checkForPostRecovery()
   * @returns {Promise<undefined>}
   */
  get postRecoveryComplete() {
    return this.#postRecoveryPromise;
  }

  /**
   * Sets the Glean restored_profile_data object metric. Reads from the
   * restored-backup-metadata pref if the profile was restored from a backup.
   *
   * @param {object} [backupMetadata=null]
   *   If provided, uses this metadata directly instead of reading from the
   *   pref. This is used by checkForPostRecovery on the first launch after
   *   a restore, where the metadata is available before it's written to the
   *   pref.
   */
  #setRestoredProfileDataMetric(backupMetadata = null) {
    const payload = {
      is_restored:
        !!Services.prefs.getIntPref(
          "browser.backup.profile-restoration-date",
          0
        ) &&
        !Services.prefs.getBoolPref("browser.profiles.profile-copied", false),
    };
    if (payload.is_restored) {
      if (!backupMetadata) {
        try {
          backupMetadata = JSON.parse(
            Services.prefs.getStringPref(
              RESTORED_BACKUP_METADATA_PREF_NAME,
              "{}"
            )
          );
        } catch {
          backupMetadata = {};
        }
      }
      payload.backup_timestamp = backupMetadata.date
        ? new Date(backupMetadata.date).getTime()
        : null;
      payload.backup_app_name = backupMetadata.appName || null;
      payload.backup_app_version = backupMetadata.appVersion || null;
      payload.backup_build_id = backupMetadata.buildID || null;
      payload.backup_os_name = backupMetadata.osName || null;
      payload.backup_os_version = backupMetadata.osVersion || null;
      payload.backup_os_build_number = backupMetadata.osBuildNumber || null;
      payload.backup_legacy_client_id = backupMetadata.healthTelemetryEnabled
        ? backupMetadata.legacyClientID || null
        : null;
      payload.intermediate_profile_creation_date =
        backupMetadata.intermediateProfileCreationDate ?? null;
      payload.restore_source = backupMetadata.restoreSource || null;
    }
    Glean.browserBackup.restoredProfileData.set(payload);
  }

  /**
   * Returns a state object describing the state of the BackupService for the
   * purposes of representing it in the user interface. The returned state
   * object is immutable.
   *
   * @type {object}
   */
  get state() {
    if (
      !Object.keys(this.#_state.defaultParent).length ||
      !this.#_state.defaultParent.path
    ) {
      let defaultPath = BackupService.DEFAULT_PARENT_DIR_PATH;
      this.#_state.defaultParent = {
        path: defaultPath,
        fileName: defaultPath ? PathUtils.filename(defaultPath) : "",
        iconURL: defaultPath ? this.getIconFromFilePath(defaultPath) : "",
      };
    }

    return Object.freeze(structuredClone(this.#_state));
  }

  /**
   * Attempts to find the right folder to write the single-file archive to, creating
   * it if it does not exist yet.
   *
   * @param {string} configuredDestFolderPath
   *   The currently configured destination folder for the archive.
   * @returns {Promise<string, Error>}
   */
  async resolveArchiveDestFolderPath(configuredDestFolderPath) {
    try {
      await IOUtils.makeDirectory(configuredDestFolderPath, {
        createAncestors: true,
        ignoreExisting: true,
      });
      if (Services.sysinfo.getProperty("name") === "Windows_NT") {
        // On Windows, adding a desktop.ini file to the folder and setting the
        // required properties allows us to display a language-translated name
        // for the folder.
        await this.#createDesktopIni(configuredDestFolderPath);
      }
      return configuredDestFolderPath;
    } catch (e) {
      lazy.logConsole.warn("Could not create configured destination path: ", e);
      throw new BackupError(
        "Could not resolve to a writable destination folder path.",
        ERRORS.FILE_SYSTEM_ERROR
      );
    }
  }

  /**
   * Computes the appropriate link to place in the single-file archive for
   * downloading a version of this application for the same update channel.
   *
   * When bug 1905909 lands, we'll first check to see if there are download
   * links available in Remote Settings.
   *
   * If there aren't any, we will fallback by looking for preference values at
   * browser.backup.template.fallback-download.${updateChannel}.
   *
   * If no such preference exists, a final "ultimate" fallback download link is
   * chosen for the release channel.
   *
   * @param {string} updateChannel
   *  The current update channel for the application, as provided by
   *  AppConstants.MOZ_UPDATE_CHANNEL.
   * @returns {Promise<string>}
   */
  async resolveDownloadLink(updateChannel) {
    // If all else fails, this is the download link we'll put into the rendered
    // template.
    const ULTIMATE_FALLBACK_DOWNLOAD_URL =
      "https://www.firefox.com/?utm_medium=firefox-desktop&utm_source=html-backup";
    const FALLBACK_DOWNLOAD_URL = Services.prefs.getStringPref(
      `browser.backup.template.fallback-download.${updateChannel}`,
      ULTIMATE_FALLBACK_DOWNLOAD_URL
    );

    // Bug 1905909: Once we set up the download links in RemoteSettings, we can
    // query for them here.

    return FALLBACK_DOWNLOAD_URL;
  }

  /**
   * Creates a backup for a given profile into a staging foler.
   *
   * @param {string} profilePath The path to the profile to backup.
   * @param {ArchiveEncryptionState|null|undefined} [encState=undefined]
   *   If supplied, overrides the ArchiveEncryptionState that the BackupService
   *   uses by default with one supplied the caller. If this is null, this means
   *   that the backup will not be encrypted. If this is undefined, this will
   *   fallback to using the ArchiveEncryptionState that BackupService uses by
   *   default.
   * @returns {Promsie<object>} An object containing the results of this function.
   * @property {STEPS} currentStep The current step of the backup process.
   * @property {string} backupDirPath The path to the folder containing backups.
   *   Only included if this function completed successfully.
   * @property {string} stagingPath The path to the staging folder.
   *   Only included if this function completed successfully.
   * @property {object} manifest An object containing meta data for the backup.
   *   See the BackupManifest schema for the specific shape of the returned
   *   manifest object.
   * @property {Error} error An error. Only included if an error was thrown.
   */
  async createAndPopulateStagingFolder(profilePath, encState) {
    let currentStep, backupDirPath, renamedStagingPath, manifest;
    try {
      currentStep = BACKUP_STEPS.CREATE_BACKUP_CREATE_MANIFEST;
      manifest = await this.#createBackupManifest();

      currentStep = BACKUP_STEPS.CREATE_BACKUP_CREATE_BACKUPS_FOLDER;
      // First, check to see if a `backups` directory already exists in the
      // profile.
      backupDirPath = PathUtils.join(
        profilePath,
        BackupService.PROFILE_FOLDER_NAME,
        BackupService.SNAPSHOTS_FOLDER_NAME
      );
      lazy.logConsole.debug("Creating backups folder");

      // ignoreExisting: true is the default, but we're being explicit that it's
      // okay if this folder already exists.
      await IOUtils.makeDirectory(backupDirPath, {
        ignoreExisting: true,
        createAncestors: true,
      });

      currentStep = BACKUP_STEPS.CREATE_BACKUP_CREATE_STAGING_FOLDER;
      let stagingPath = await this.#prepareStagingFolder(backupDirPath);

      // Sort resources be priority.
      let sortedResources = Array.from(this.#resources.values()).sort(
        (a, b) => {
          return b.priority - a.priority;
        }
      );

      if (encState === undefined) {
        currentStep = BACKUP_STEPS.CREATE_BACKUP_LOAD_ENCSTATE;
        encState = await this.loadEncryptionState(profilePath);
      } else {
        lazy.logConsole.debug("Using encState param: ", encState);
      }

      let encryptionEnabled = !!encState;
      lazy.logConsole.debug("Encryption enabled: ", encryptionEnabled);

      currentStep = BACKUP_STEPS.CREATE_BACKUP_RUN_BACKUP;
      // Perform the backup for each resource.
      for (let resourceClass of sortedResources) {
        try {
          lazy.logConsole.debug(
            `Backing up resource with key ${resourceClass.key}. ` +
              `Requires encryption: ${resourceClass.requiresEncryption}`
          );

          if (resourceClass.requiresEncryption && !encryptionEnabled) {
            lazy.logConsole.debug(
              "Encryption is not currently enabled. Skipping."
            );
            continue;
          }

          if (!resourceClass.canBackupResource) {
            lazy.logConsole.debug(
              `We cannot backup ${resourceClass.key}. Skipping.`
            );
            continue;
          }

          let resourcePath = PathUtils.join(stagingPath, resourceClass.key);
          await IOUtils.makeDirectory(resourcePath);

          // `backup` on each BackupResource should return us a ManifestEntry
          // that we eventually write to a JSON manifest file, but for now,
          // we're just going to log it.
          let manifestEntry = await new resourceClass().backup(
            resourcePath,
            profilePath,
            encryptionEnabled
          );

          if (manifestEntry === undefined) {
            lazy.logConsole.error(
              `Backup of resource with key ${resourceClass.key} returned undefined
                as its ManifestEntry instead of null or an object`
            );
          } else {
            lazy.logConsole.debug(
              `Backup of resource with key ${resourceClass.key} completed`,
              manifestEntry
            );
            manifest.resources[resourceClass.key] = manifestEntry;
          }
        } catch (e) {
          lazy.logConsole.error(
            `Failed to backup resource: ${resourceClass.key}`,
            e
          );
        }
      }

      currentStep = BACKUP_STEPS.CREATE_BACKUP_VERIFY_MANIFEST;
      // Ensure that the manifest abides by the current schema, and log
      // an error if somehow it doesn't. We'll want to collect telemetry for
      // this case to make sure it's not happening in the wild. We debated
      // throwing an exception here too, but that's not meaningfully better
      // than creating a backup that's not schema-compliant. At least in this
      // case, a user so-inclined could theoretically repair the manifest
      // to make it valid.
      let manifestSchema = await BackupService.MANIFEST_SCHEMA;
      let schemaValidationResult = lazy.JsonSchema.validate(
        manifest,
        manifestSchema
      );
      if (!schemaValidationResult.valid) {
        lazy.logConsole.error(
          "Backup manifest does not conform to schema:",
          manifest,
          manifestSchema,
          schemaValidationResult
        );
        // TODO: Collect telemetry for this case. (bug 1891817)
      }

      currentStep = BACKUP_STEPS.CREATE_BACKUP_WRITE_MANIFEST;
      // Write the manifest to the staging folder.
      let manifestPath = PathUtils.join(
        stagingPath,
        BackupService.MANIFEST_FILE_NAME
      );
      await IOUtils.writeJSON(manifestPath, manifest);

      currentStep = BACKUP_STEPS.CREATE_BACKUP_FINALIZE_STAGING;
      renamedStagingPath = await this.#finalizeStagingFolder(stagingPath);
      lazy.logConsole.log(
        "Wrote backup to staging directory at ",
        renamedStagingPath
      );

      // Record the total size of the backup staging directory
      let totalSizeKilobytes =
        await BackupResource.getDirectorySize(renamedStagingPath);
      let totalSizeBytesNearestMebibyte = MeasurementUtils.fuzzByteSize(
        totalSizeKilobytes * BYTES_IN_KILOBYTE,
        1 * BYTES_IN_MEBIBYTE
      );
      lazy.logConsole.debug(
        "total staging directory size in bytes: " +
          totalSizeBytesNearestMebibyte
      );

      Glean.browserBackup.totalBackupSize.accumulate(
        totalSizeBytesNearestMebibyte / BYTES_IN_MEBIBYTE
      );
    } catch (e) {
      return { currentStep, error: e };
    }

    return {
      currentStep,
      backupDirPath,
      stagingPath: renamedStagingPath,
      manifest,
    };
  }

  /**
   * @typedef {object} CreateBackupResult
   * @property {object} manifest
   *   The backup manifest data of the created backup. See BackupManifest
   *   schema for specific details.
   * @property {string} archivePath
   *   The path to the single file archive that was created.
   */

  /**
   * Create a backup of the user's profile.
   *
   * @param {object} [options]
   *   Options for the backup.
   * @param {string} [options.profilePath=PathUtils.profileDir]
   *   The path to the profile to backup. By default, this is the current
   *   profile.
   * @param {string} [options.reason=unknown]
   *   The reason for starting the backup. This is sent along with the
   *   backup.backup_start event.
   * @param {ArchiveEncryptionState|null} [options.encState=undefined]
   *   Callers can supply an override to the current encryption state if they,
   *   for example, want to create a one-off encrypted backup with a different
   *   passphrase, or want to create a backup without encryption when encryption
   *   is still enabled. When undefined, this will use the current default
   *   encryption state. When null, this will create the backup without
   *   encryption.
   * @returns {Promise<CreateBackupResult|null>}
   *   A promise that resolves to information about the backup that was
   *   created, or null if the backup failed.
   */
  async createBackup({
    profilePath = PathUtils.profileDir,
    reason = "unknown",
    encState,
  } = {}) {
    let status = this.archiveEnabledStatus;
    if (!status.enabled) {
      lazy.logConsole.debug(status.reason);
      return null;
    }

    // createBackup does not allow re-entry or concurrent backups.
    if (this.#backupInProgress) {
      lazy.logConsole.warn("Backup attempt already in progress");
      return null;
    }

    Glean.browserBackup.backupStart.record({ reason });

    if (encState === undefined) {
      encState = await this.loadEncryptionState(profilePath);
    }

    return locks.request(
      BackupService.WRITE_BACKUP_LOCK_NAME,
      { signal: this.#backupWriteAbortController.signal },
      async () => {
        let currentStep = BACKUP_STEPS.CREATE_BACKUP_ENTRYPOINT;
        this.#backupInProgress = true;
        const backupTimer = Glean.browserBackup.totalBackupTime.start();

        // reset the error state prefs
        Services.prefs.clearUserPref(BACKUP_DEBUG_INFO_PREF_NAME);
        Services.prefs.setIntPref(BACKUP_ERROR_CODE_PREF_NAME, ERRORS.NONE);
        // reset profile copied pref so the backup welcome messaging will show
        Services.prefs.clearUserPref("browser.profiles.profile-copied");

        try {
          lazy.logConsole.debug(
            `Creating backup for profile at ${profilePath}`
          );

          currentStep = BACKUP_STEPS.CREATE_BACKUP_RESOLVE_DESTINATION;
          let archiveDestFolderPath = await this.resolveArchiveDestFolderPath(
            lazy.backupDirPref
          );
          lazy.logConsole.debug(
            `Destination for archive: ${archiveDestFolderPath}`
          );

          let result = await this.createAndPopulateStagingFolder(
            profilePath,
            encState
          );
          currentStep = result.currentStep;
          if (result.error) {
            // Re-throw the error so we can catch it below for telemetry
            throw result.error;
          }

          let { backupDirPath, stagingPath, manifest } = result;

          currentStep = BACKUP_STEPS.CREATE_BACKUP_COMPRESS_STAGING;
          let compressedStagingPath = await this.#compressStagingFolder(
            stagingPath,
            backupDirPath
          ).finally(async () => {
            // retryReadonly is needed in case there were read only files in
            // the profile.
            await IOUtils.remove(stagingPath, {
              recursive: true,
              retryReadonly: true,
            });
          });

          currentStep = BACKUP_STEPS.CREATE_BACKUP_CREATE_ARCHIVE;
          // Now create the single-file archive. For now, we'll stash this in the
          // backups folder while it gets written. Once that's done, we'll attempt
          // to move it to the user's configured backup path.
          let archiveTmpPath = PathUtils.join(backupDirPath, "archive.html");
          lazy.logConsole.log(
            "Exporting single-file archive to ",
            archiveTmpPath
          );
          await this.createArchive(
            archiveTmpPath,
            BackupService.ARCHIVE_TEMPLATE,
            compressedStagingPath,
            encState,
            manifest.meta
          ).finally(async () => {
            await IOUtils.remove(compressedStagingPath, {
              retryReadonly: true,
            });
          });

          // Record the size of the complete single-file archive
          let archiveSizeKilobytes =
            await BackupResource.getFileSize(archiveTmpPath);
          let archiveSizeBytesNearestMebibyte = MeasurementUtils.fuzzByteSize(
            archiveSizeKilobytes * BYTES_IN_KILOBYTE,
            1 * BYTES_IN_MEBIBYTE
          );
          lazy.logConsole.debug(
            "backup archive size in bytes: " + archiveSizeBytesNearestMebibyte
          );

          Glean.browserBackup.compressedArchiveSize.accumulate(
            archiveSizeBytesNearestMebibyte / BYTES_IN_MEBIBYTE
          );

          currentStep = BACKUP_STEPS.CREATE_BACKUP_FINALIZE_ARCHIVE;
          let archivePath = await this.finalizeSingleFileArchive(
            archiveTmpPath,
            archiveDestFolderPath,
            manifest.meta
          );

          let nowSeconds = Math.floor(Date.now() / 1000);
          Services.prefs.setIntPref(
            LAST_BACKUP_TIMESTAMP_PREF_NAME,
            nowSeconds
          );
          this.#_state.lastBackupDate = nowSeconds;
          Glean.browserBackup.totalBackupTime.stopAndAccumulate(backupTimer);

          Glean.browserBackup.created.record({
            encrypted: this.#_state.encryptionEnabled,
            location: this.classifyLocationForTelemetry(archiveDestFolderPath),
            size: archiveSizeBytesNearestMebibyte,
          });

          // It's possible that our profile was initially a legacy profile, but somewhere
          // sometime got converted to a selectable one - lets start tracking its backup state
          // in the group.
          BackupService.maybeAddToEnabledListPref();

          // we should reset any values that were set for retry error handling
          Services.prefs.clearUserPref(DISABLED_ON_IDLE_RETRY_PREF_NAME);
          BackupService.#errorRetries = 0;

          return { manifest, archivePath };
        } catch (e) {
          Glean.browserBackup.totalBackupTime.cancel(backupTimer);
          Glean.browserBackup.error.record({
            error_code: String(e.cause || ERRORS.UNKNOWN),
            backup_step: String(currentStep),
          });

          // TODO: show more specific error messages to the user
          Services.prefs.setIntPref(
            BACKUP_ERROR_CODE_PREF_NAME,
            ERRORS.UNKNOWN
          );

          Services.prefs.setStringPref(
            BACKUP_DEBUG_INFO_PREF_NAME,
            JSON.stringify({
              lastBackupAttempt: Math.floor(Date.now() / 1000),
              errorCode: e instanceof BackupError ? e : ERRORS.UNKNOWN,
              lastRunStep: currentStep,
            })
          );

          this.stateUpdate();
          throw e;
        } finally {
          this.#backupInProgress = false;
        }
      }
    );
  }

  /**
   * Creates a coarse name corresponding to the location where the backup will
   * be stored. This is sent by telemetry, and aims to anonymize the data.
   *
   * Normally, the path should end in 'Restore Firefox'; if it doesn't, you
   * might be passing the wrong path and will get the wrong result.
   *
   * This isn't private so it can be used by the tests; avoid relying on this
   * code from elsewhere.
   *
   * @param {string} path The absolute path that contains the backup file.
   * @returns {string} A coarse location to send with the telemetry.
   */
  classifyLocationForTelemetry(path) {
    let knownLocations = {
      onedrive: "OneDrPD",
      documents: "Docs",
    };

    let location;
    try {
      // Backup files live inside a subfolder (e.g. "Restore Firefox") under
      // the actual save location (Documents, OneDrive, etc.), so we need the
      // grandparent of the file.
      location = lazy.nsLocalFile(path).parent?.parent;
    } catch (e) {
      // initWithPath (at least on Windows) is _really_ picky; e.g.
      // "C:/Windows/system32" will fail. Bail out if something went wrong so
      // this doesn't affect the backup.
      return `Error: ${e.name ?? "Unknown error"}`;
    }

    for (let label of Object.keys(knownLocations)) {
      try {
        let candidate = Services.dirsvc.get(knownLocations[label], Ci.nsIFile);
        if (candidate.equals(location)) {
          return label;
        }
      } catch (e) {
        // ignore (maybe it wasn't found?)
      }
    }

    return "other";
  }

  /**
   * Generates a string from a Date in the form of:
   *
   * YYYYMMDD-HHMM
   *
   * @param {Date} date
   *   The date to convert into the archive date suffix.
   * @returns {string}
   */
  generateArchiveDateSuffix(date) {
    let year = date.getFullYear().toString();

    // In all cases, months or days with single digits are expected to start
    // with a 0.

    // Note that getMonth() is 0-indexed for some reason, so we increment by 1.
    let month = `${date.getMonth() + 1}`.padStart(2, "0");

    let day = `${date.getDate()}`.padStart(2, "0");
    let hours = `${date.getHours()}`.padStart(2, "0");
    let minutes = `${date.getMinutes()}`.padStart(2, "0");
    let seconds = `${date.getSeconds()}`.padStart(2, "0");
    let millis = `${date.getMilliseconds()}`.padStart(3, "0");

    return `${year}${month}${day}-${hours}${minutes}${seconds}.${millis}`;
  }

  /**
   * Moves the single-file archive into its configured location with a filename
   * that is sanitized and contains a timecode. This also removes any existing
   * single-file archives in that same folder after the move completes.
   *
   * @param {string} sourcePath
   *   The file system location of the single-file archive prior to the move.
   * @param {string} destFolder
   *   The folder that the single-file archive is configured to be eventually
   *   written to.
   * @param {object} metadata
   *   The metadata for the backup. See the BackupManifest schema for details.
   * @returns {Promise<string>}
   *   Resolves with the path that the single-file archive was moved to.
   */
  async finalizeSingleFileArchive(sourcePath, destFolder, metadata) {
    let archiveDateSuffix = this.generateArchiveDateSuffix(
      new Date(metadata.date)
    );

    let existingChildren = await IOUtils.getChildren(destFolder);

    // construct prefix
    let nameParts = [BackupService.BACKUP_FILE_NAME, metadata.profileName];
    let storeID = lazy.SelectableProfileService.storeID;
    if (storeID) {
      nameParts.push(storeID);
    }
    const FILENAME_PREFIX = nameParts.join("_");

    const FILENAME = `${FILENAME_PREFIX}_${archiveDateSuffix}.html`;
    let destPath = PathUtils.join(destFolder, FILENAME);

    lazy.logConsole.log("Moving single-file archive to ", destPath);
    await IOUtils.move(sourcePath, destPath);

    Services.prefs.setStringPref(LAST_BACKUP_FILE_NAME_PREF_NAME, FILENAME);

    for (let childFilePath of existingChildren) {
      let childFileName = PathUtils.filename(childFilePath);
      // We check both the prefix and the suffix, because the prefix encodes
      // the profile name in it. If there are other profiles from the same
      // application performing backup, we don't want to accidentally remove
      // those.
      if (
        childFileName.startsWith(FILENAME_PREFIX) &&
        childFileName.endsWith(".html")
      ) {
        if (childFileName == FILENAME) {
          // Since filenames don't include seconds, this might occur if a
          // backup was created seconds after the last one during the same
          // minute. That tends not to happen in practice, but might occur
          // during testing, in which case, we'll skip clearing this file.
          lazy.logConsole.warn(
            "Collided with a pre-existing archive name, so not clearing: ",
            FILENAME
          );
          continue;
        }
        lazy.logConsole.debug("Getting rid of ", childFilePath);
        await IOUtils.remove(childFilePath);
      }
    }

    return destPath;
  }

  /**
   * Constructs the staging folder for the backup in the passed in backup
   * folder. If the backup (snapshots) folder isn't empty, it will be cleared
   * out.  If that process fails to remove more than
   * lazy.maximumNumberOfUnremovableStagingItems then the backup is aborted.
   *
   * @param {string} backupDirPath
   *   The path to the backup folder.
   * @returns {Promise<string>}
   *   The path to the empty staging folder.
   */
  async #prepareStagingFolder(backupDirPath) {
    lazy.logConsole.debug(`Clearing snapshot folder ${backupDirPath}`);
    let numUnremovableStagingItems = 0;
    let folderEntries = await IOUtils.getChildren(backupDirPath, {
      ignoreAbsent: true,
    });
    if (folderEntries) {
      let unremovableContents = [];
      for (let folderItem of folderEntries) {
        try {
          lazy.logConsole.debug(`Removing ${folderItem}`);
          await IOUtils.remove(folderItem, {
            recursive: true,
            retryReadonly: true,
          });
        } catch (e) {
          lazy.logConsole.warn(
            `Failed to remove stale snapshot item ${folderItem}.  Exception: ${e}`
          );
          // Whatever the problem was with removing the snapshot dir contents
          // (presumably a staging dir or archive), keep going until
          // maximumNumberOfUnremovableStagingItems + 1 have failed to be
          // removed, at which point we abandon the backup, in order to avoid
          // filling drive space.
          numUnremovableStagingItems++;
          unremovableContents.push(folderItem);
          if (
            numUnremovableStagingItems >
            lazy.maximumNumberOfUnremovableStagingItems
          ) {
            let error = new BackupError(
              `Failed to remove ${numUnremovableStagingItems} items from ${backupDirPath}`,
              ERRORS.FILE_SYSTEM_ERROR
            );
            error.stack = e.stack;
            error.unremovableContents = unremovableContents;
            throw error;
          }
        }
      }
    }

    lazy.logConsole.debug(
      `${numUnremovableStagingItems} unremovable staging items found.  Proceeding with backup.  Determining staging folder.`
    );
    let stagingPath;
    for (let i = 0; i < lazy.maximumNumberOfUnremovableStagingItems + 1; i++) {
      // Attempt to use "staging-i" as the name of the staging folder.
      let potentialStagingPath = PathUtils.join(backupDirPath, "staging-" + i);
      if (!(await IOUtils.exists(potentialStagingPath))) {
        stagingPath = potentialStagingPath;
        await IOUtils.makeDirectory(stagingPath);
        break;
      }
    }

    if (!stagingPath) {
      // Should be impossible.  We determined there were no more than
      // maximumNumberOfUnremovableStagingItems items but we then found
      // maximumNumberOfUnremovableStagingItems + 1 staging folders.
      throw new BackupError(
        `Internal error in attempt to create staging folder`,
        ERRORS.FILE_SYSTEM_ERROR
      );
    }

    lazy.logConsole.debug(`Staging folder ${stagingPath} is prepared`);
    return stagingPath;
  }

  /**
   * Compresses a staging folder into a Zip file. If a pre-existing Zip file
   * for a staging folder resides in destFolderPath, it is overwritten. The
   * Zip file will have the same name as the stagingPath folder, with `.zip`
   * as the extension.
   *
   * @param {string} stagingPath
   *   The path to the staging folder to be compressed.
   * @param {string} destFolderPath
   *   The parent folder to write the Zip file to.
   * @returns {Promise<string>}
   *   Resolves with the path to the created Zip file.
   */
  async #compressStagingFolder(stagingPath, destFolderPath) {
    const PR_RDWR = 0x04;
    const PR_CREATE_FILE = 0x08;
    const PR_TRUNCATE = 0x20;

    let archivePath = PathUtils.join(
      destFolderPath,
      `${PathUtils.filename(stagingPath)}.zip`
    );
    let archiveFile = await IOUtils.getFile(archivePath);

    let writer = new lazy.ZipWriter(
      archiveFile,
      PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE
    );

    lazy.logConsole.log("Compressing staging folder to ", archivePath);
    let rootPathNSIFile = await IOUtils.getDirectory(stagingPath);
    await this.#compressChildren(rootPathNSIFile, stagingPath, writer);
    await new Promise(resolve => {
      let observer = {
        onStartRequest(_request) {
          lazy.logConsole.debug("Starting to write out archive file");
        },
        onStopRequest(_request, status) {
          lazy.logConsole.log("Done writing archive file");
          resolve(status);
        },
      };
      writer.processQueue(observer, null);
    });
    writer.close();

    return archivePath;
  }

  /**
   * A helper function for #compressStagingFolder that iterates through a
   * directory, and adds each file to a nsIZipWriter. For each directory it
   * finds, it recurses.
   *
   * @param {nsIFile} rootPathNSIFile
   *   An nsIFile pointing at the root of the folder being compressed.
   * @param {string} parentPath
   *   The path to the folder whose children should be iterated.
   * @param {nsIZipWriter} writer
   *   The writer to add all of the children to.
   * @returns {Promise<undefined>}
   */
  async #compressChildren(rootPathNSIFile, parentPath, writer) {
    let children = await IOUtils.getChildren(parentPath);
    for (let childPath of children) {
      let childState = await IOUtils.stat(childPath);
      if (childState.type == "directory") {
        await this.#compressChildren(rootPathNSIFile, childPath, writer);
      } else {
        let childFile = await IOUtils.getFile(childPath);
        // nsIFile.getRelativePath returns paths using the "/" separator,
        // regardless of which platform we're on. That's handy, because this
        // is the same separator that nsIZipWriter expects for entries.
        let pathRelativeToRoot = childFile.getRelativePath(rootPathNSIFile);
        writer.addEntryFile(
          pathRelativeToRoot,
          BackupService.COMPRESSION_LEVEL,
          childFile,
          true
        );
      }
    }
  }

  /**
   * Decompressed a compressed recovery file into recoveryFolderDestPath.
   *
   * @param {string} recoveryFilePath
   *   The path to the compressed recovery file to decompress.
   * @param {string} recoveryFolderDestPath
   *   The path to the folder that the compressed recovery file should be
   *   decompressed within.
   * @returns {Promise<undefined>}
   */
  async decompressRecoveryFile(recoveryFilePath, recoveryFolderDestPath) {
    let recoveryFile = await IOUtils.getFile(recoveryFilePath);
    let recoveryArchive = new lazy.ZipReader(recoveryFile);
    lazy.logConsole.log(
      "Decompressing recovery folder to ",
      recoveryFolderDestPath
    );
    try {
      // null is passed to test if we're meant to CRC test the entire
      // ZIP file. If an exception is thrown, this means we failed the CRC
      // check. See the nsIZipReader.idl documentation for details.
      recoveryArchive.test(null);
    } catch (e) {
      recoveryArchive.close();
      lazy.logConsole.error("Compressed recovery file was corrupt.");
      await IOUtils.remove(recoveryFilePath, {
        retryReadonly: true,
      });
      throw new BackupError("Corrupt archive.", ERRORS.CORRUPTED_ARCHIVE);
    }

    try {
      await this.#decompressChildren(
        recoveryFolderDestPath,
        "",
        recoveryArchive
      );
    } catch (e) {
      recoveryArchive.close();
      throw e instanceof BackupError
        ? e
        : new BackupError(
            `Failed to decompress recovery file: ${e.message}`,
            ERRORS.DECOMPRESSION_FAILED
          );
    }
    recoveryArchive.close();
  }

  /**
   * A helper method that recursively decompresses any children within a folder
   * within a compressed archive.
   *
   * @param {string} rootPath
   *   The path to the root folder that is being decompressed into.
   * @param {string} parentEntryName
   *   The name of the parent folder within the compressed archive that is
   *   having its children decompressed.
   * @param {nsIZipReader} reader
   *   The nsIZipReader for the compressed archive.
   * @returns {Promise<undefined>}
   */
  async #decompressChildren(rootPath, parentEntryName, reader) {
    // nsIZipReader.findEntries has an interesting querying language that is
    // documented in the nsIZipReader IDL file, in case you're curious about
    // what these symbols mean.
    let childEntryNames = reader.findEntries(
      parentEntryName + "?*~" + parentEntryName + "?*/?*"
    );

    for (let childEntryName of childEntryNames) {
      let childEntry = reader.getEntry(childEntryName);
      if (childEntry.isDirectory) {
        await this.#decompressChildren(rootPath, childEntryName, reader);
      } else {
        let inputStream = reader.getInputStream(childEntryName);
        // ZIP files all use `/` as their path separators, regardless of
        // platform.
        let fileNameParts = childEntryName.split("/");
        let outputFilePath = PathUtils.join(rootPath, ...fileNameParts);
        let outputFile = await IOUtils.getFile(outputFilePath);
        let outputStream = Cc[
          "@mozilla.org/network/file-output-stream;1"
        ].createInstance(Ci.nsIFileOutputStream);

        outputStream.init(
          outputFile,
          -1,
          -1,
          Ci.nsIFileOutputStream.DEFER_OPEN
        );

        await new Promise(resolve => {
          lazy.logConsole.debug("Writing ", outputFilePath);
          lazy.NetUtil.asyncCopy(inputStream, outputStream, () => {
            lazy.logConsole.debug("Done writing ", outputFilePath);
            outputStream.close();
            resolve();
          });
        });
      }
    }
  }

  /**
   * Given a URI to an HTML template for the single-file backup archive,
   * produces the static markup that will then be used as the beginning of that
   * single-file backup archive.
   *
   * @param {string} templateURI
   *   A URI pointing at a template for the HTML content for the page. This is
   *   what is visible if the file is loaded in a web browser.
   * @param {boolean} isEncrypted
   *   True if the template should indicate that the backup is encrypted.
   * @param {object} backupMetadata
   *   The metadata for the backup, which is also stored in the backup manifest
   *   of the compressed backup snapshot.
   * @returns {Promise<string>}
   */
  async renderTemplate(templateURI, isEncrypted, backupMetadata) {
    const ARCHIVE_STYLES = "chrome://browser/content/backup/archive.css";
    const ARCHIVE_SCRIPT = "chrome://browser/content/backup/archive.js";
    const LOGO = "chrome://branding/content/icon128.png";

    let templateResponse = await fetch(templateURI);
    let templateString = await templateResponse.text();
    let templateDOM = new DOMParser().parseFromString(
      templateString,
      "text/html"
    );

    // Set the lang attribute on the <html> element
    templateDOM.documentElement.setAttribute(
      "lang",
      Services.locale.appLocaleAsBCP47
    );

    let downloadLink = templateDOM.querySelector("#download-moz-browser");
    downloadLink.href = await this.resolveDownloadLink(
      AppConstants.MOZ_UPDATE_CHANNEL
    );

    let supportURI = new URL(
      "firefox-backup",
      Services.urlFormatter.formatURLPref("app.support.baseURL")
    );
    supportURI.searchParams.set("utm_medium", "firefox-desktop");
    supportURI.searchParams.set("utm_source", "html-backup");
    supportURI.searchParams.set("utm_campaign", "fx-backup-restore");

    let supportLink = templateDOM.querySelector("#support-link");
    supportLink.href = supportURI.href;

    // Now insert the logo as a dataURL, since we want the single-file backup
    // archive to be entirely self-contained.
    let logoResponse = await fetch(LOGO);
    let logoBlob = await logoResponse.blob();
    let logoDataURL = await new Promise((resolve, reject) => {
      let reader = new FileReader();
      reader.addEventListener("load", () => resolve(reader.result));
      reader.addEventListener("error", reject);
      reader.readAsDataURL(logoBlob);
    });

    let logoNode = templateDOM.querySelector("#logo");
    logoNode.src = logoDataURL;

    let encStateNode = templateDOM.querySelector("#encryption-state-value");
    lazy.gDOMLocalization.setAttributes(
      encStateNode,
      isEncrypted
        ? "backup-file-encryption-state-value-encrypted"
        : "backup-file-encryption-state-value-not-encrypted"
    );

    let createdDateNode = templateDOM.querySelector("#creation-date-value");
    lazy.gDOMLocalization.setArgs(createdDateNode, {
      // It's very unlikely that backupMetadata.date isn't a valid Date string,
      // but if it _is_, then Fluent will cause us to crash in debug builds.
      // We fallback to the current date if all else fails.
      date: new Date(backupMetadata.date).getTime() || new Date().getTime(),
    });

    let creationDeviceNode = templateDOM.querySelector(
      "#creation-device-value"
    );
    creationDeviceNode.textContent = backupMetadata.machineName;

    try {
      await lazy.gDOMLocalization.translateFragment(
        templateDOM.documentElement
      );
    } catch (_) {
      // This shouldn't happen, but we don't want a missing locale string to
      // cause backup creation to fail.
    }

    // We have to insert styles and scripts after we serialize to XML, otherwise
    // the XMLSerializer will escape things like descendent selectors in CSS
    // with &gt;.
    let stylesResponse = await fetch(ARCHIVE_STYLES);
    let scriptResponse = await fetch(ARCHIVE_SCRIPT);

    // These days, we don't really support CSS preprocessor directives, so we
    // can't ifdef out the MPL license header in styles before writing it into
    // the archive file. Instead, we'll ensure that the license header is there,
    // and then manually remove it here at runtime.
    let stylesText = await stylesResponse.text();
    const MPL_LICENSE = `/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */`;
    if (!stylesText.includes(MPL_LICENSE)) {
      throw new BackupError(
        "Expected the MPL license block within archive.css",
        ERRORS.UNKNOWN
      );
    }

    stylesText = stylesText.replace(MPL_LICENSE, "");

    let serializer = new XMLSerializer();
    return serializer
      .serializeToString(templateDOM)
      .replace("{{styles}}", stylesText)
      .replace("{{script}}", await scriptResponse.text());
  }

  /**
   * Creates a portable, potentially encrypted single-file archive containing
   * a compressed backup snapshot. The single-file archive is a specially
   * crafted HTML file that embeds the compressed backup snapshot and
   * backup metadata.
   *
   * @param {string} archivePath
   *   The path to write the single-file archive to.
   * @param {string} templateURI
   *   A URI pointing at a template for the HTML content for the page. This is
   *   what is visible if the file is loaded in a web browser.
   * @param {string} compressedBackupSnapshotPath
   *   The path on the file system where the compressed backup snapshot exists.
   * @param {ArchiveEncryptionState|null} encState
   *   The ArchiveEncryptionState to encrypt the backup with, if encryption is
   *   enabled. If null is passed, the backup will not be encrypted.
   * @param {object} backupMetadata
   *   The metadata for the backup, which is also stored in the backup manifest
   *   of the compressed backup snapshot.
   * @param {object} options
   *   Options to pass to the worker, mainly for testing.
   * @param {object} [options.chunkSize=ArchiveUtils.ARCHIVE_CHUNK_MAX_BYTES_SIZE]
   *   The chunk size to break the bytes into.
   */
  async createArchive(
    archivePath,
    templateURI,
    compressedBackupSnapshotPath,
    encState,
    backupMetadata,
    options = {}
  ) {
    let markup = await this.renderTemplate(
      templateURI,
      !!encState,
      backupMetadata
    );

    let worker = new lazy.BasePromiseWorker(
      "moz-src:///browser/components/backup/Archive.worker.mjs",
      { type: "module" }
    );
    worker.ExceptionHandlers[BackupError.name] = BackupError.fromMsg;

    let chunkSize =
      options.chunkSize || lazy.ArchiveUtils.ARCHIVE_CHUNK_MAX_BYTES_SIZE;

    try {
      let encryptionArgs = encState
        ? {
            publicKey: encState.publicKey,
            salt: encState.salt,
            nonce: encState.nonce,
            backupAuthKey: encState.backupAuthKey,
            wrappedSecrets: encState.wrappedSecrets,
          }
        : null;

      await worker
        .post("constructArchive", [
          {
            archivePath,
            markup,
            backupMetadata,
            compressedBackupSnapshotPath,
            encryptionArgs,
            chunkSize,
          },
        ])
        .catch(e => {
          lazy.logConsole.error(e);
          if (!(e instanceof BackupError)) {
            throw new BackupError("Failed to create archive", ERRORS.UNKNOWN);
          }
          throw e;
        });
    } finally {
      worker.terminate();
    }
  }

  /**
   * Constructs an nsIChannel that serves the bytes from an nsIInputStream -
   * specifically, a nsIInputStream of bytes being streamed from a file.
   *
   * @see BackupService.#extractMetadataFromArchive()
   * @param {nsIInputStream} inputStream
   *   The nsIInputStream to create the nsIChannel for.
   * @param {string} contentType
   *   The content type for the nsIChannel. This is provided by
   *   BackupService.#extractMetadataFromArchive().
   * @returns {nsIChannel}
   */
  #createExtractionChannel(inputStream, contentType) {
    let uri = "http://localhost";
    let httpChan = lazy.NetUtil.newChannel({
      uri,
      loadUsingSystemPrincipal: true,
    });

    let channel = Cc["@mozilla.org/network/input-stream-channel;1"]
      .createInstance(Ci.nsIInputStreamChannel)
      .QueryInterface(Ci.nsIChannel);

    channel.setURI(httpChan.URI);
    channel.loadInfo = httpChan.loadInfo;

    channel.contentStream = inputStream;
    channel.contentType = contentType;
    return channel;
  }

  /**
   * A helper for BackupService.extractCompressedSnapshotFromArchive() that
   * reads in the JSON block from the MIME message embedded within an
   * archiveFile.
   *
   * @see BackupService.extractCompressedSnapshotFromArchive()
   * @param {nsIFile} archiveFile
   *   The file to read the MIME message out from.
   * @param {number} startByteOffset
   *   The start byte offset of the MIME message.
   * @param {string} contentType
   *   The Content-Type of the MIME message.
   * @returns {Promise<object>}
   */
  async #extractJSONFromArchive(archiveFile, startByteOffset, contentType) {
    let fileInputStream = Cc[
      "@mozilla.org/network/file-input-stream;1"
    ].createInstance(Ci.nsIFileInputStream);
    fileInputStream.init(
      archiveFile,
      -1,
      -1,
      Ci.nsIFileInputStream.CLOSE_ON_EOF
    );
    fileInputStream.seek(Ci.nsISeekableStream.NS_SEEK_SET, startByteOffset);

    const EXPECTED_CONTENT_TYPE = "application/json";

    let extractionChannel = this.#createExtractionChannel(
      fileInputStream,
      contentType
    );
    let textDecoder = new TextDecoder();
    return new Promise((resolve, reject) => {
      let streamConv = Cc["@mozilla.org/streamConverters;1"].getService(
        Ci.nsIStreamConverterService
      );
      let multipartListenerForJSON = {
        /**
         * True once we've found an attachment matching our
         * EXPECTED_CONTENT_TYPE. Once this is true, bytes flowing into
         * onDataAvailable will be enqueued through the controller.
         *
         * @type {boolean}
         */
        _enabled: false,

        /**
         * True once onStopRequest has been called once the listener is enabled.
         * After this, the listener will not attempt to read any data passed
         * to it through onDataAvailable.
         *
         * @type {boolean}
         */
        _done: false,

        /**
         * A buffer with which we will cobble together the JSON string that
         * will get parsed once the attachment finishes being read in.
         *
         * @type {string}
         */
        _buffer: "",

        QueryInterface: ChromeUtils.generateQI([
          "nsIStreamListener",
          "nsIRequestObserver",
          "nsIMultiPartChannelListener",
        ]),

        /**
         * Called when we begin to load an attachment from the MIME message.
         *
         * @param {nsIRequest} request
         *   The request corresponding to the source of the data.
         */
        onStartRequest(request) {
          if (!(request instanceof Ci.nsIChannel)) {
            throw Components.Exception(
              "onStartRequest expected an nsIChannel request",
              Cr.NS_ERROR_UNEXPECTED
            );
          }
          this._enabled = request.contentType == EXPECTED_CONTENT_TYPE;
        },

        /**
         * Called when data is flowing in for an attachment.
         *
         * @param {nsIRequest} request
         *   The request corresponding to the source of the data.
         * @param {nsIInputStream} stream
         *   The input stream containing the data chunk.
         * @param {number} offset
         *   The number of bytes that were sent in previous onDataAvailable
         *   calls for this request. In other words, the sum of all previous
         *   count parameters.
         * @param {number} count
         *   The number of bytes available in the stream
         */
        onDataAvailable(request, stream, offset, count) {
          if (this._done) {
            // No need to load anything else - abort reading in more
            // attachments.
            throw Components.Exception(
              "Got JSON block. Aborting further reads.",
              Cr.NS_BINDING_ABORTED
            );
          }
          if (!this._enabled) {
            // We don't care about this data, just move on.
            return;
          }

          let binStream = new lazy.BinaryInputStream(stream);
          let arrBuffer = new ArrayBuffer(count);
          binStream.readArrayBuffer(count, arrBuffer);
          let jsonBytes = new Uint8Array(arrBuffer);
          this._buffer += textDecoder.decode(jsonBytes);
        },

        /**
         * Called when the load of an attachment finishes.
         */
        onStopRequest() {
          if (this._enabled && !this._done) {
            this._enabled = false;
            this._done = true;

            try {
              let archiveMetadata = JSON.parse(this._buffer);
              resolve(archiveMetadata);
            } catch (e) {
              reject(
                new BackupError(
                  "Could not parse archive metadata.",
                  ERRORS.CORRUPTED_ARCHIVE
                )
              );
            }
          }
        },

        onAfterLastPart() {
          if (!this._done) {
            // We finished reading the parts before we found the JSON block, so
            // the JSON block is missing.
            reject(
              new BackupError(
                "Could not find JSON block.",
                ERRORS.CORRUPTED_ARCHIVE
              )
            );
          }
        },
      };
      let conv = streamConv.asyncConvertData(
        "multipart/mixed",
        "*/*",
        multipartListenerForJSON,
        null
      );

      extractionChannel.asyncOpen(conv);
    });
  }

  /**
   * A helper for BackupService.#extractCompressedSnapshotFromArchive that
   * constructs a BinaryReadableStream for a single-file archive on the
   * file system. The BinaryReadableStream will be used to read out the binary
   * attachment from the archive.
   *
   * @param {nsIFile} archiveFile
   *   The single-file archive to create the BinaryReadableStream for.
   * @param {number} startByteOffset
   *   The start byte offset of the MIME message.
   * @param {string} contentType
   *   The Content-Type of the MIME message.
   * @returns {ReadableStream}
   */
  async createBinaryReadableStream(archiveFile, startByteOffset, contentType) {
    let fileInputStream = Cc[
      "@mozilla.org/network/file-input-stream;1"
    ].createInstance(Ci.nsIFileInputStream);
    fileInputStream.init(
      archiveFile,
      -1,
      -1,
      Ci.nsIFileInputStream.CLOSE_ON_EOF
    );
    fileInputStream.seek(Ci.nsISeekableStream.NS_SEEK_SET, startByteOffset);

    let extractionChannel = this.#createExtractionChannel(
      fileInputStream,
      contentType
    );

    return new ReadableStream(new BinaryReadableStream(extractionChannel));
  }

  /**
   * @typedef {object} SampleArchiveResult
   * @property {boolean} isEncrypted
   *   True if the archive claims to be encrypted, and has the necessary data
   *   within the JSON block to attempt to initialize an ArchiveDecryptor.
   * @property {number} startByteOffset
   *   The start byte offset of the MIME message.
   * @property {string} contentType
   *   The Content-Type of the MIME message.
   * @property {object} archiveJSON
   *   The deserialized JSON block from the archive. See the ArchiveJSONBlock
   *   schema for details of its structure.
   */

  /**
   * Reads from a file to determine if it seems to be a backup archive, and if
   * so, resolves with some information about the archive without actually
   * unpacking it. The returned Promise may reject if the file does not appear
   * to be a backup archive, or the backup archive appears to have been
   * corrupted somehow.
   *
   * @param {string} archivePath
   *   The path to the archive file to sample.
   * @returns {Promise<SampleArchiveResult, Error>}
   */
  async sampleArchive(archivePath) {
    let worker = new lazy.BasePromiseWorker(
      "moz-src:///browser/components/backup/Archive.worker.mjs",
      { type: "module" }
    );
    worker.ExceptionHandlers[BackupError.name] = BackupError.fromMsg;

    if (!(await IOUtils.exists(archivePath))) {
      throw new BackupError(
        "Archive file does not exist at path " + archivePath,
        ERRORS.UNKNOWN
      );
    }

    try {
      let { startByteOffset, contentType } = await worker
        .post("parseArchiveHeader", [archivePath])
        .catch(e => {
          lazy.logConsole.error(e);
          if (!(e instanceof BackupError)) {
            throw new BackupError(
              "Failed to parse archive header",
              ERRORS.CORRUPTED_ARCHIVE
            );
          }
          throw e;
        });
      let archiveFile = await IOUtils.getFile(archivePath);
      let archiveJSON;
      try {
        archiveJSON = await this.#extractJSONFromArchive(
          archiveFile,
          startByteOffset,
          contentType
        );

        if (!archiveJSON.version) {
          throw new BackupError(
            "Missing version in the archive JSON block.",
            ERRORS.CORRUPTED_ARCHIVE
          );
        }
        if (archiveJSON.version > lazy.ArchiveUtils.SCHEMA_VERSION) {
          throw new BackupError(
            `Archive JSON block is a version newer than we can interpret: ${archiveJSON.version}`,
            ERRORS.UNSUPPORTED_BACKUP_VERSION
          );
        }

        let archiveJSONSchema = await BackupService.getSchemaForVersion(
          SCHEMAS.ARCHIVE_JSON_BLOCK,
          archiveJSON.version
        );

        let manifestSchema = await BackupService.getSchemaForVersion(
          SCHEMAS.BACKUP_MANIFEST,
          archiveJSON.version
        );

        let validator = new lazy.JsonSchema.Validator(archiveJSONSchema);
        validator.addSchema(manifestSchema);

        let schemaValidationResult = validator.validate(archiveJSON);
        if (!schemaValidationResult.valid) {
          lazy.logConsole.error(
            "Archive JSON block does not conform to schema:",
            archiveJSON,
            archiveJSONSchema,
            schemaValidationResult
          );

          // TODO: Collect telemetry for this case. (bug 1891817)
          throw new BackupError(
            `Archive JSON block does not conform to schema version ${archiveJSON.version}`,
            ERRORS.CORRUPTED_ARCHIVE
          );
        }
      } catch (e) {
        lazy.logConsole.error(e);
        throw e;
      }

      lazy.logConsole.debug("Read out archive JSON: ", archiveJSON);

      return {
        isEncrypted: !!archiveJSON.encConfig,
        startByteOffset,
        contentType,
        archiveJSON,
      };
    } catch (e) {
      lazy.logConsole.error(e);
      throw e;
    } finally {
      worker.terminate();
    }
  }

  /**
   * Attempts to extract the compressed backup snapshot from a single-file
   * archive, and write the extracted file to extractionDestPath. This may
   * reject if the single-file archive appears malformed or cannot be
   * properly decrypted. If the backup was encrypted, a native nsIOSKeyStore
   * is also initialized with label BackupService.RECOVERY_OSKEYSTORE_LABEL
   * with the secret used on the original backup machine. Callers are
   * responsible for clearing this secret after any decryptions with it are
   * completed.
   *
   * NOTE: Currently, this base64 decoding currently occurs on the main thread.
   * We may end up moving all of this into the Archive Worker if we can modify
   * IOUtils to allow writing via a stream.
   *
   * @param {string} archivePath
   *   The single-file archive that contains the backup.
   * @param {string} extractionDestPath
   *   The path to write the extracted file to.
   * @param {string} [recoveryCode=null]
   *   The recovery code to decrypt an encrypted backup with.
   * @returns {Promise<{isEncrypted: boolean}, Error>}
   *   Resolves with whether the archive was encrypted.
   */
  async extractCompressedSnapshotFromArchive(
    archivePath,
    extractionDestPath,
    recoveryCode = null
  ) {
    let { isEncrypted, startByteOffset, contentType, archiveJSON } =
      await this.sampleArchive(archivePath);

    let decryptor = null;
    if (isEncrypted) {
      if (!recoveryCode) {
        throw new BackupError(
          "A recovery code is required to decrypt this archive.",
          ERRORS.UNAUTHORIZED
        );
      }
      decryptor = await lazy.ArchiveDecryptor.initialize(
        recoveryCode,
        archiveJSON
      );
    }

    await IOUtils.remove(extractionDestPath, {
      ignoreAbsent: true,
      retryReadonly: true,
    });

    let archiveFile = await IOUtils.getFile(archivePath);
    let archiveStream = await this.createBinaryReadableStream(
      archiveFile,
      startByteOffset,
      contentType
    );

    let binaryDecoder = new TransformStream(
      new DecoderDecryptorTransformer(decryptor)
    );
    let fileWriter = new WritableStream(
      new FileWriterStream(extractionDestPath, decryptor)
    );
    try {
      await archiveStream.pipeThrough(binaryDecoder).pipeTo(fileWriter);
    } catch (e) {
      throw e instanceof BackupError
        ? e
        : new BackupError(
            `Failed to extract snapshot: ${e?.message || e}`,
            ERRORS.CORRUPTED_ARCHIVE
          );
    }

    if (decryptor) {
      await lazy.nativeOSKeyStore.asyncRecoverSecret(
        BackupService.RECOVERY_OSKEYSTORE_LABEL,
        decryptor.OSKeyStoreSecret
      );
    }

    return { isEncrypted };
  }

  /**
   * Renames the staging folder to an ISO 8601 date string with dashes replacing colons and fractional seconds stripped off.
   * The ISO date string should be formatted from YYYY-MM-DDTHH:mm:ss.sssZ to YYYY-MM-DDTHH-mm-ssZ
   *
   * @param {string} stagingPath
   *   The path to the populated staging folder.
   * @returns {Promise<string|null>}
   *   The path to the renamed staging folder, or null if the stagingPath was
   *   not pointing to a valid folder.
   */
  async #finalizeStagingFolder(stagingPath) {
    if (!(await IOUtils.exists(stagingPath))) {
      // If we somehow can't find the specified staging folder, cancel this step.
      lazy.logConsole.error(
        `Failed to finalize staging folder. Cannot find ${stagingPath}.`
      );
      return null;
    }

    try {
      lazy.logConsole.debug("Finalizing and renaming staging folder");
      let currentDateISO = new Date().toISOString();
      // First strip the fractional seconds
      let dateISOStripped = currentDateISO.replace(/\.\d+\Z$/, "Z");
      // Now replace all colons with dashes
      let dateISOFormatted = dateISOStripped.replaceAll(":", "-");

      let stagingPathParent = PathUtils.parent(stagingPath);
      let renamedBackupPath = PathUtils.join(
        stagingPathParent,
        dateISOFormatted
      );
      await IOUtils.move(stagingPath, renamedBackupPath);

      let existingBackups = await IOUtils.getChildren(stagingPathParent);

      /**
       * Bug 1892532: for now, we only support a single backup file.
       * If there are other pre-existing backup folders, delete them - but don't
       * delete anything that doesn't match the backup folder naming scheme.
       */
      let expectedFormatRegex = /\d{4}(-\d{2}){2}T(\d{2}-){2}\d{2}Z/;
      for (let existingBackupPath of existingBackups) {
        if (
          existingBackupPath !== renamedBackupPath &&
          existingBackupPath.match(expectedFormatRegex)
        ) {
          try {
            // If any copied source files were read-only then we need to remove
            // read-only status from them to delete the staging folder.
            await IOUtils.remove(existingBackupPath, {
              recursive: true,
              retryReadonly: true,
            });
          } catch (e) {
            // Ignore any failures in removing staging items.
            lazy.logConsole.debug(
              `Failed to remove staging item ${existingBackupPath}. Exception ${e}`
            );
          }
        }
      }
      return renamedBackupPath;
    } catch (e) {
      lazy.logConsole.error(
        `Something went wrong while finalizing the staging folder. ${e}`
      );
      throw new BackupError(
        "Failed to finalize staging folder",
        ERRORS.FILE_SYSTEM_ERROR
      );
    }
  }

  /**
   * Creates and resolves with a backup manifest object with an empty resources
   * property. See the BackupManifest schema for the specific shape of the
   * returned manifest object.
   *
   * @returns {Promise<object>}
   */
  async #createBackupManifest() {
    let profileSvc = Cc["@mozilla.org/toolkit/profile-service;1"].getService(
      Ci.nsIToolkitProfileService
    );
    let profileName;
    if (!profileSvc.currentProfile) {
      // We're probably running on a local build or in some special configuration.
      // Let's pull in a profile name from the profile directory.
      let profileFolder = PathUtils.split(PathUtils.profileDir).at(-1);
      profileName = profileFolder.substring(profileFolder.indexOf(".") + 1);
    } else if (lazy.SelectableProfileService.currentProfile) {
      profileName = lazy.SelectableProfileService.currentProfile.name;
    } else {
      profileName = profileSvc.currentProfile.name;
    }

    let meta = {
      date: new Date().toISOString(),
      appName: AppConstants.MOZ_APP_NAME,
      appVersion: AppConstants.MOZ_APP_VERSION,
      buildID: AppConstants.MOZ_BUILDID,
      profileName,
      deviceName: Services.sysinfo.get("device") || Services.dns.myHostName,
      machineName: lazy.fxAccounts.device.getLocalName(),
      osName: Services.sysinfo.getProperty("name"),
      osVersion: Services.sysinfo.getProperty("version"),
      osBuildNumber: (() => {
        try {
          return Services.sysinfo.getProperty("build");
        } catch {
          return null;
        }
      })(),
      legacyClientID: await lazy.ClientID.getClientID(),
      profileGroupID: await lazy.ClientID.getProfileGroupID(),
      healthTelemetryEnabled: Services.prefs.getBoolPref(
        "datareporting.healthreport.uploadEnabled",
        false
      ),
      usageTelemetryEnabled: Services.prefs.getBoolPref(
        "datareporting.usage.uploadEnabled",
        false
      ),
      isSelectableProfile: !!lazy.SelectableProfileService.currentProfile,
    };

    let fxaState = lazy.UIState.get();
    if (fxaState.status == lazy.UIState.STATUS_SIGNED_IN) {
      meta.accountID = fxaState.uid;
      meta.accountEmail = fxaState.email;
    }

    return {
      version: lazy.ArchiveUtils.SCHEMA_VERSION,
      meta,
      resources: {},
    };
  }

  /**
   * Given a backup archive at archivePath, this method does the
   * following:
   *
   * 1. Potentially decrypts, and then extracts the compressed backup snapshot
   *    from the archive to a file named BackupService.RECOVERY_ZIP_FILE_NAME in
   *    the PROFILE_FOLDER_NAME folder.
   * 2. Decompresses that file into a subdirectory of PROFILE_FOLDER_NAME named
   *    "recovery".
   * 3. Deletes the BackupService.RECOVERY_ZIP_FILE_NAME file.
   * 4. Calls into recoverFromSnapshotFolder on the decompressed "recovery"
   *    folder.
   * 5. Optionally launches the newly created profile.
   * 6. Returns the name of the newly created profile directory.
   *
   * @see BackupService.recoverFromSnapshotFolder
   * @param {string} archivePath
   *   The path to the single-file backup archive on the file system.
   * @param {string|null} recoveryCode
   *   The recovery code to use to attempt to decrypt the archive if it was
   *   encrypted.
   * @param {boolean} [shouldLaunchOrQuit=false]
   *   An optional argument that specifies whether an instance of the app
   *   should be launched or allowed to quit after recovery is complete.
   * @param {boolean} [profilePath=PathUtils.profileDir]
   *   The profile path where the recovery files will be written to within the
   *   PROFILE_FOLDER_NAME. This is only used for testing.
   * @param {string} [profileRootPath=null]
   *   An optional argument that specifies the root directory where the new
   *   profile directory should be created. If not provided, the default
   *   profile root directory will be used. This is primarily meant for
   *   testing.
   * @param {boolean} [replaceCurrentProfile=false]
   *   An optional argument that determines if the backed up profile should replace
   *   the current profile, or add a new profile.
   * @param {string} [source=null]
   *   Which UI initiated the restore (e.g. "onboarding", "preferences").
   * @returns {Promise<nsIToolkitProfile>}
   *   The nsIToolkitProfile that was created for the recovered profile.
   * @throws {Exception}
   *   In the event that unpacking the archive, decompressing the snapshot, or
   *   recovery from the snapshot somehow failed.
   */
  async recoverFromBackupArchive(
    archivePath,
    recoveryCode = null,
    shouldLaunchOrQuit = false,
    profilePath = PathUtils.profileDir,
    profileRootPath = null,
    replaceCurrentProfile = false,
    source = null
  ) {
    const status = this.restoreEnabledStatus;
    if (!status.enabled) {
      throw new Error(status.reason);
    }

    // No concurrent recoveries.
    if (this.#_state.recoveryInProgress) {
      lazy.logConsole.warn("Recovery attempt already in progress");
      return null;
    }

    Glean.browserBackup.restoreStarted.record({
      restore_id: this.#_state.restoreID,
      replace: replaceCurrentProfile,
      backup_version: this.#_state.backupFileInfo?.appVersion || "",
      backup_os_name: this.#_state.backupFileInfo?.osName || "",
      backup_os_version: this.#_state.backupFileInfo?.osVersion || "",
      backup_os_build_number: this.#_state.backupFileInfo?.osBuildNumber || "",
    });

    let currentStep = RESTORE_STEPS.RESTORE_ENTRYPOINT;
    try {
      this.#_state.recoveryInProgress = true;
      this.#_state.recoveryErrorCode = 0;

      try {
        let profileAge = await lazy.ProfileAge();
        this.#_state.intermediateProfileCreationDate = await profileAge.created;
      } catch (e) {
        lazy.logConsole.warn("Failed to get intermediate profile date", e);
        this.#_state.intermediateProfileCreationDate = null;
      }

      this.#_state.restoreSource = source;
      this.stateUpdate();
      const RECOVERY_FILE_DEST_PATH = PathUtils.join(
        profilePath,
        BackupService.PROFILE_FOLDER_NAME,
        BackupService.RECOVERY_ZIP_FILE_NAME
      );
      currentStep = RESTORE_STEPS.RESTORE_EXTRACT_SNAPSHOT;
      let { isEncrypted } =
        (await this.extractCompressedSnapshotFromArchive(
          archivePath,
          RECOVERY_FILE_DEST_PATH,
          recoveryCode
        )) ?? {};

      const RECOVERY_FOLDER_DEST_PATH = PathUtils.join(
        profilePath,
        BackupService.PROFILE_FOLDER_NAME,
        "recovery"
      );
      currentStep = RESTORE_STEPS.RESTORE_DECOMPRESS;
      await this.decompressRecoveryFile(
        RECOVERY_FILE_DEST_PATH,
        RECOVERY_FOLDER_DEST_PATH
      );

      // Now that we've decompressed it, reclaim some disk space by getting rid of
      // the ZIP file.
      try {
        await IOUtils.remove(RECOVERY_FILE_DEST_PATH, { retryReadonly: true });
      } catch (_) {
        lazy.logConsole.warn("Could not remove ", RECOVERY_FILE_DEST_PATH);
      }

      try {
        // We're using a try/finally here to clean up the temporary OSKeyStore.
        // We need to make sure that cleanup occurs _after_ the recovery has
        // either fully succeeded, or fully failed. We await the return value
        // of recoverFromSnapshotFolder so that the finally will not execute
        // until after recoverFromSnapshotFolder has finished resolving or
        // rejecting.

        currentStep = RESTORE_STEPS.RESTORE_READ_MANIFEST;
        let manifest = await this.#readAndValidateManifest(
          RECOVERY_FOLDER_DEST_PATH
        );

        const replacingLegacyWithLegacy =
          replaceCurrentProfile && manifest.meta?.isSelectableProfile === false;

        // Before we do a bunch of work, let's decide if selectable profiles are allowed on this device.
        // If they aren't, we should always default to not recovering the SelectableProfileBackupResource.
        currentStep = RESTORE_STEPS.RESTORE_PROFILE_SETUP;
        if (!lazy.SelectableProfileService.isEnabled) {
          delete manifest.resources[
            DefaultBackupResources.SelectableProfileBackupResource.key
          ];
        } // Otherwise, we might have to convert the current profile into a selectable profile
        else if (
          !lazy.SelectableProfileService.hasCreatedSelectableProfiles() &&
          !replacingLegacyWithLegacy
        ) {
          // Convert to selectable when adding a legacy backup or replacing with a selectable backup.
          // Deletion (if needed) is handled in maybeDeleteAndQuitCurrentSelectableProfile.
          try {
            lazy.logConsole.debug(
              `Converting current legacy profile into a selectable profile`
            );
            await lazy.SelectableProfileService.maybeSetupDataStore();
          } catch (e) {
            // TODO: Currently, we just throw in this case, but we should be able to
            // just fallback to recovering everything but the SelectableProfileBackupResource.
            throw new BackupError(
              `something went wrong when converting the current profile into a selectableProfile: ${e}`,
              ERRORS.PROFILE_CREATION_FAILED
            );
          }
        }

        currentStep = RESTORE_STEPS.RESTORE_CREATE_PROFILE;
        let newProfile;
        if (lazy.SelectableProfileService.currentProfile) {
          newProfile =
            await this.recoverFromSnapshotFolderIntoSelectableProfile(
              RECOVERY_FOLDER_DEST_PATH,
              shouldLaunchOrQuit,
              null,
              profileRootPath,
              manifest,
              replaceCurrentProfile,
              isEncrypted
            );
        } else {
          newProfile = await this.recoverFromSnapshotFolder(
            RECOVERY_FOLDER_DEST_PATH,
            shouldLaunchOrQuit,
            profileRootPath,
            manifest,
            isEncrypted
          );
        }

        currentStep = RESTORE_STEPS.RESTORE_FINALIZE;
        Glean.browserBackup.restoreComplete.record({
          restore_id: this.#_state.restoreID,
          backup_version: this.#_state.backupFileInfo?.appVersion || "",
          backup_os_name: this.#_state.backupFileInfo?.osName || "",
          backup_os_version: this.#_state.backupFileInfo?.osVersion || "",
          backup_os_build_number:
            this.#_state.backupFileInfo?.osBuildNumber || "",
        });
        // We are probably about to shutdown, so we want to submit this ASAP.
        // But this will also clear out the data in this ping, which is a bit
        // of a problem for testing. So fire off an event first that tests can
        // listen for.
        Services.obs.notifyObservers(null, "browser-backup-restore-complete");
        GleanPings.profileRestore.submit();

        // Looks like everything went well, if we are replacing the current profile
        // we are good to close and delete it now
        if (replaceCurrentProfile) {
          try {
            await this.deleteAndQuitCurrentSelectableProfile(
              shouldLaunchOrQuit
            );
          } catch (e) {
            lazy.logConsole.error(
              "Failed to delete and quit current profile after successful restore",
              e
            );
          }
        }

        return newProfile;
      } finally {
        // If we had decrypted a backup, we would have created the temporary
        // recovery OSKeyStore row with the label
        // BackupService.RECOVERY_OSKEYSTORE_LABEL, which we will now delete,
        // no matter if we succeeded or failed to recover.
        //
        // Note that according to nsIOSKeyStore, this is a no-op in the event that
        // no secret exists at BackupService.RECOVERY_OSKEYSTORE_LABEL, so we're
        // fine to do this even if we were recovering from an unencrypted
        // backup.
        if (recoveryCode) {
          await lazy.nativeOSKeyStore.asyncDeleteSecret(
            BackupService.RECOVERY_OSKEYSTORE_LABEL
          );
        }
      }
    } catch (ex) {
      let restoreStep = ex.resourceKey
        ? "RECOVER_RESOURCE:" + ex.resourceKey
        : ex.restoreStep || currentStep;
      Glean.browserBackup.restoreFailed.record({
        restore_id: this.#_state.restoreID,
        error_type:
          ex instanceof BackupError
            ? errorString(ex.cause)
            : errorString(ERRORS.RECOVERY_FAILED),
        error_detail:
          ex.resourceKey ||
          (!(ex instanceof BackupError) && ex.message
            ? ex.message.substring(0, 100)
            : ""),
        restore_step: restoreStep,
        backup_version: this.#_state.backupFileInfo?.appVersion || "",
        backup_os_name: this.#_state.backupFileInfo?.osName || "",
        backup_os_version: this.#_state.backupFileInfo?.osVersion || "",
        backup_os_build_number:
          this.#_state.backupFileInfo?.osBuildNumber || "",
      });
      throw ex;
    } finally {
      this.#_state.recoveryInProgress = false;
      this.stateUpdate();
    }
  }

  /**
   * Handles deleting the currently running selectable profile
   * and then quitting it.
   *
   * @param {boolean} [shouldQuit=true]
   * Optional param to disable actually quitting the running instance. Used
   * primarily for testing.
   */
  async deleteAndQuitCurrentSelectableProfile(shouldQuit = true) {
    if (!lazy.SelectableProfileService.hasCreatedSelectableProfiles()) {
      lazy.logConsole.warn(
        `We don't delete the running profile in the case of legacy backup to legacy recovery`
      );
      return null;
    }

    // Notify windows that a quit has been requested.
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(cancelQuit, "quit-application-requested");

    if (cancelQuit.data) {
      // Something blocked our attempt to quit.
      return null;
    }

    try {
      await lazy.SelectableProfileService.deleteCurrentProfile();

      if (shouldQuit) {
        // Finally, exit.
        Services.startup.quit(Ci.nsIAppStartup.eAttemptQuit);
      }
    } catch (e) {
      // This is expected in tests.
      lazy.logConsole.error(`Errored while attempting delete and quit: ${e}`);
    }
    return null;
  }

  /**
   * Given a recovery path, read in the backup manifest from the archive and
   * ensures that it is valid. Will throw an error for an invalid manifest.
   *
   * @param {string} recoveryPath The path to the decompressed backup archive
   *   on the file system.
   * @returns {object} See the BackupManifest schema for the specific shape of the
   * returned manifest object.
   */
  async #readAndValidateManifest(recoveryPath) {
    // Read in the backup manifest.
    let manifestPath = PathUtils.join(
      recoveryPath,
      BackupService.MANIFEST_FILE_NAME
    );

    let manifest;
    try {
      manifest = await IOUtils.readJSON(manifestPath);
    } catch (e) {
      throw new BackupError(
        `Failed to read backup manifest: ${e.message}`,
        ERRORS.CORRUPTED_ARCHIVE
      );
    }
    if (!manifest.version) {
      throw new BackupError(
        "Backup manifest version not found",
        ERRORS.CORRUPTED_ARCHIVE
      );
    }

    if (manifest.version > lazy.ArchiveUtils.SCHEMA_VERSION) {
      throw new BackupError(
        "Cannot recover from a manifest newer than the current schema version",
        ERRORS.UNSUPPORTED_BACKUP_VERSION
      );
    }

    // Make sure that it conforms to the schema.
    let manifestSchema = await BackupService.getSchemaForVersion(
      SCHEMAS.BACKUP_MANIFEST,
      manifest.version
    );
    let schemaValidationResult = lazy.JsonSchema.validate(
      manifest,
      manifestSchema
    );
    if (!schemaValidationResult.valid) {
      lazy.logConsole.error(
        "Backup manifest does not conform to schema:",
        manifest,
        manifestSchema,
        schemaValidationResult
      );
      // TODO: Collect telemetry for this case. (bug 1891817)
      throw new BackupError(
        "Cannot recover from an invalid backup manifest",
        ERRORS.CORRUPTED_ARCHIVE
      );
    }

    // Manifest version compatibility
    if (manifest.version < 2) {
      manifest.isSelectableProfile = false;
    }

    let meta = manifest.meta;

    if (meta.appName != AppConstants.MOZ_APP_NAME) {
      throw new BackupError(
        `Cannot recover a backup from ${meta.appName} in ${AppConstants.MOZ_APP_NAME}`,
        ERRORS.UNSUPPORTED_APPLICATION
      );
    }

    if (
      Services.vc.compare(AppConstants.MOZ_APP_VERSION, meta.appVersion) < 0
    ) {
      throw new BackupError(
        `Cannot recover a backup created on version ${meta.appVersion} in ${AppConstants.MOZ_APP_VERSION}`,
        ERRORS.UNSUPPORTED_BACKUP_VERSION
      );
    }

    return manifest;
  }

  /**
   * Extract the active theme ID from a legacy backup's prefs.js.
   *
   * @param {string} recoveryPath The path to the decompressed backup archive.
   * @returns {string} The theme addon ID, or the default theme ID as fallback.
   */
  async #getLegacyThemeId(recoveryPath) {
    const DEFAULT_THEME_ID = "default-theme@mozilla.org";
    let prefsPath = PathUtils.join(
      recoveryPath,
      DefaultBackupResources.PreferencesBackupResource.key,
      "prefs.js"
    );
    try {
      let prefsBuffer = await IOUtils.read(prefsPath);
      let prefs =
        DefaultBackupResources.PreferencesBackupResource.getPrefsFromBuffer(
          prefsBuffer,
          ["extensions.activeThemeID"]
        );
      return prefs.get("extensions.activeThemeID") || DEFAULT_THEME_ID;
    } catch (e) {
      lazy.logConsole.warn("Could not read legacy theme from prefs.js", e);
      return DEFAULT_THEME_ID;
    }
  }

  /**
   * Iterates over each resource in the manifest and calls the recover() method
   * on each found BackupResource, passing in the associated ManifestEntry from
   * the backup manifest, and collects any post-recovery data from those
   * resources.
   *
   * @param {object} manifest See the BackupManifest schema for the specific
   *   shape of the returned manifest object.
   * @param {string} recoveryPath The path to the decompressed backup archive
   *   on the file system.
   * @param {string} profilePath The path of the newly recovered profile
   * @param {boolean} wasEncrypted Whether the source archive was encrypted.
   *   Resources that require encryption are skipped when this is false.
   * @returns {object}
   *   An object containing post recovery data for each resource.
   */
  async #recoverResources(manifest, recoveryPath, profilePath, wasEncrypted) {
    let postRecovery = {};

    // Iterate over each resource in the manifest and call recover() on each
    // associated BackupResource.
    for (let resourceKey in manifest.resources) {
      let manifestEntry = manifest.resources[resourceKey];
      let resourceClass = this.#resources.get(resourceKey);
      if (!resourceClass) {
        lazy.logConsole.error(`No BackupResource found for key ${resourceKey}`);
        continue;
      }

      // A resource that requires encryption should only have been written into
      // an encrypted archive. If the archive isn't encrypted, refuse to recover it.
      if (resourceClass.requiresEncryption && !wasEncrypted) {
        lazy.logConsole.warn(
          `Skipping resource ${resourceKey}: requires encryption but the ` +
            `archive is not encrypted.`
        );
        continue;
      }

      try {
        lazy.logConsole.debug(
          `Restoring resource with key ${resourceKey}. ` +
            `Requires encryption: ${resourceClass.requiresEncryption}`
        );
        let resourcePath = PathUtils.join(recoveryPath, resourceKey);
        let postRecoveryEntry = await new resourceClass().recover(
          manifestEntry,
          resourcePath,
          profilePath
        );
        postRecovery[resourceKey] = postRecoveryEntry;
      } catch (e) {
        lazy.logConsole.error(`Failed to recover resource: ${resourceKey}`, e);
        if (e instanceof BackupError) {
          e.resourceKey = resourceKey;
          throw e;
        }
        let err = new BackupError(
          `Failed to recover resource ${resourceKey}: ${e.message}`,
          ERRORS.RESOURCE_RECOVERY_FAILED
        );
        err.resourceKey = resourceKey;
        throw err;
      }
    }

    return postRecovery;
  }

  /**
   * Write the post recovery data to the newly recovered profile.
   *
   * @param {object} postRecoveryData An object containing post recovery data
   *   from each resource recovered.
   * @param {string} profilePath The path of the newly recovered profile
   */
  async #writePostRecoveryData(postRecoveryData, profilePath) {
    let postRecoveryPath = PathUtils.join(
      profilePath,
      BackupService.POST_RECOVERY_FILE_NAME
    );
    await IOUtils.writeJSON(postRecoveryPath, postRecoveryData);
  }

  /**
   * Given a decompressed backup archive at recoveryPath, this method does the
   * following:
   *
   * 1. Reads in the backup manifest from the archive and ensures that it is
   *    valid.
   * 2. Creates a new named profile directory using the same name as the one
   *    found in the backup manifest, but with a different prefix.
   * 3. Iterates over each resource in the manifest and calls the recover()
   *    method on each found BackupResource, passing in the associated
   *    ManifestEntry from the backup manifest, and collects any post-recovery
   *    data from those resources.
   * 4. Writes a `post-recovery.json` file into the newly created profile
   *    directory.
   * 5. Returns the name of the newly created profile directory.
   * 6. Regardless of whether or not recovery succeeded, clears the native
   *    OSKeyStore of any secret labeled with
   *    BackupService.RECOVERY_OSKEYSTORE_LABEL.
   *
   * @param {string} recoveryPath
   *   The path to the decompressed backup archive on the file system.
   * @param {boolean} [shouldLaunch=false]
   *   An optional argument that specifies whether an instance of the app
   *   should be launched with the newly recovered profile after recovery is
   *   complete.
   * @param {string} [profileRootPath=null]
   *   An optional argument that specifies the root directory where the new
   *   profile directory should be created. If not provided, the default
   *   profile root directory will be used. This is primarily meant for
   *   testing.
   * @param {object} [manifest=null]
   *   If we've already read and validated the manifest, we can avoid redoing that work
   *   by passing this in as a parameter.
   * @param {boolean} [wasEncrypted=false]
   *   Whether the source archive was encrypted. Resources that require
   *   encryption are skipped during recovery when this is false.
   * @returns {Promise<nsIToolkitProfile>}
   *   The nsIToolkitProfile that was created for the recovered profile.
   * @throws {Exception}
   *   In the event that recovery somehow failed.
   */
  async recoverFromSnapshotFolder(
    recoveryPath,
    shouldLaunch = false,
    profileRootPath = null,
    manifest = null,
    wasEncrypted = false
  ) {
    lazy.logConsole.debug("Recovering from backup at ", recoveryPath);

    let restoreStep = RESTORE_STEPS.RESTORE_CREATE_PROFILE;
    try {
      if (!manifest) {
        manifest = await this.#readAndValidateManifest(recoveryPath);
      }

      // Okay, we have a valid backup-manifest.json. Let's create a new profile
      // and start invoking the recover() method on each BackupResource.
      let profileSvc = Cc["@mozilla.org/toolkit/profile-service;1"].getService(
        Ci.nsIToolkitProfileService
      );
      let profile;
      try {
        profile = profileSvc.createUniqueProfile(
          profileRootPath ? await IOUtils.getDirectory(profileRootPath) : null,
          manifest.meta.profileName,
          // This is transient and will be overwritten when times.json is copied over.
          "backup"
        );
      } catch (e) {
        throw e instanceof BackupError
          ? e
          : new BackupError(
              `Failed to create profile: ${e.message}`,
              ERRORS.PROFILE_CREATION_FAILED
            );
      }

      restoreStep = RESTORE_STEPS.RESTORE_RECOVER_RESOURCES;
      let postRecovery = await this.#recoverResources(
        manifest,
        recoveryPath,
        profile.rootDir.path,
        wasEncrypted
      );

      restoreStep = RESTORE_STEPS.RESTORE_WRITE_POST_RECOVERY;
      try {
        postRecovery.backupServiceInternal = {
          // Indicates that this is not a result of a profile copy (which uses the
          // same mechanism, but doesn't go through this function).
          isProfileRestore: true,
          restoreID: this.#_state.restoreID,
          backupMetadata: {
            date: this.#_state.backupFileInfo.date,
            appName: this.#_state.backupFileInfo.appName,
            appVersion: this.#_state.backupFileInfo.appVersion,
            buildID: this.#_state.backupFileInfo.buildID,
            osName: this.#_state.backupFileInfo.osName,
            osVersion: this.#_state.backupFileInfo.osVersion,
            osBuildNumber: this.#_state.backupFileInfo.osBuildNumber,
            legacyClientID: this.#_state.backupFileInfo.legacyClientID,
            healthTelemetryEnabled:
              this.#_state.backupFileInfo.healthTelemetryEnabled,
            intermediateProfileCreationDate:
              this.#_state.intermediateProfileCreationDate,
            restoreSource: this.#_state.restoreSource,
          },
        };
      } catch {}

      await this.#writePostRecoveryData(postRecovery, profile.rootDir.path);

      restoreStep = RESTORE_STEPS.RESTORE_CONFIGURE_PROFILE;
      // In a release scenario, this should always be true
      // this makes it easier to get around setting up profiles for testing other functionality
      if (profileSvc.currentProfile) {
        // if our current profile was default, let's make the new one default
        if (profileSvc.currentProfile === profileSvc.defaultProfile) {
          profileSvc.defaultProfile = profile;
        }

        // If the profile already has an [old-] prefix, let's skip adding new prefixes
        if (!profileSvc.currentProfile.name.startsWith("old-")) {
          // Looks like this is a new restoration of this profile,
          // add the prefix old-[profile_name]
          profileSvc.currentProfile.name = `old-${profileSvc.currentProfile.name}`;
        }
      }

      await profileSvc.asyncFlush();

      restoreStep = RESTORE_STEPS.RESTORE_LAUNCH_PROFILE;
      if (shouldLaunch) {
        // Launch with the user's default homepage instead of the last selected tab
        // to avoid problems with the messaging system (see Bug 2002732)
        Services.startup.createInstanceWithProfile(profile, [
          "--url",
          "about:home",
        ]);
      }

      return profile;
    } catch (e) {
      lazy.logConsole.error(
        "Failed to recover from backup at ",
        recoveryPath,
        e
      );
      let err =
        e instanceof BackupError
          ? e
          : new BackupError(
              `Recovery failed: ${e.message}`,
              ERRORS.RECOVERY_FAILED
            );
      err.restoreStep = err.restoreStep || restoreStep;
      throw err;
    }
  }

  /**
   * Given a decompressed backup archive at recoveryPath, this method does the
   * following:
   *
   * 1. Reads in the backup manifest from the archive and ensures that it is
   *    valid.
   * 2. Creates a new SelectableProfile profile directory using the same name
   *    as the one found in the backup manifest, but with a different prefix.
   * 3. Iterates over each resource in the manifest and calls the recover()
   *    method on each found BackupResource, passing in the associated
   *    ManifestEntry from the backup manifest, and collects any post-recovery
   *    data from those resources.
   * 4. Writes a `post-recovery.json` file into the newly created profile
   *    directory.
   * 5. Returns the name of the newly created profile directory.
   * 6. Regardless of whether or not recovery succeeded, clears the native
   *    OSKeyStore of any secret labeled with
   *    BackupService.RECOVERY_OSKEYSTORE_LABEL.
   *
   * @param {string} recoveryPath
   *   The path to the decompressed backup archive on the file system.
   * @param {boolean} [shouldLaunch=false]
   *   An optional argument that specifies whether an instance of the app
   *   should be launched with the newly recovered profile after recovery is
   *   complete.
   * @param {SelectableProfile} [copiedProfile=null]
   *   If the profile we are recovering is a "copied" profile, we don't want to
   *   inherit the client ID as this profile will be a new profile in the
   *   profile group. If we are copying a profile, we will use
   *   copiedProfile.name to show that the new profile is a copy of
   *   copiedProfile on about:editprofile.
   * @param {string} [profileRootPath=null]
   *   Optional path where the new profile directory should be created.
   *   If not provided, the default profile location will be used.
   * @param {object} [manifest=null]
   *   If we've already read and validated the manifest, we can avoid redoing that work
   *   by passing this in as a parameter.
   * @param {boolean} [replaceCurrentProfile=false]
   *   Indicates if we are replacing the current running profile or adding to the group
   * @param {boolean} [wasEncrypted=false]
   *   Whether the source archive was encrypted. Resources that require
   *   encryption are skipped during recovery when this is false.
   * @returns {Promise<SelectableProfile>}
   *   The SelectableProfile that was created for the recovered profile.
   * @throws {Exception}
   *   In the event that recovery somehow failed.
   */
  async recoverFromSnapshotFolderIntoSelectableProfile(
    recoveryPath,
    shouldLaunch = false,
    copiedProfile = null,
    profileRootPath = null,
    manifest = null,
    replaceCurrentProfile = false,
    wasEncrypted = false
  ) {
    lazy.logConsole.debug(
      "Recovering SelectableProfile from backup at ",
      recoveryPath
    );

    let restoreStep = RESTORE_STEPS.RESTORE_CREATE_PROFILE;
    try {
      if (!manifest) {
        manifest = await this.#readAndValidateManifest(recoveryPath);
      }

      // Okay, we have a valid backup-manifest.json. Let's create a new profile
      // and start invoking the recover() method on each BackupResource.
      let profile;
      try {
        let existingProfilePath = null;
        if (profileRootPath) {
          let profileDirName = `recovered-${Date.now()}`;
          let profileDirPath = PathUtils.join(profileRootPath, profileDirName);
          await IOUtils.makeDirectory(profileDirPath, { permissions: 0o700 });
          existingProfilePath = await IOUtils.getDirectory(profileDirPath);
        }
        profile = await lazy.SelectableProfileService.createNewProfile(
          false,
          existingProfilePath,
          // This is transient and will be overwritten when times.json is copied over.
          "backup"
        );
      } catch (e) {
        throw e instanceof BackupError
          ? e
          : new BackupError(
              `Failed to create selectable profile: ${e.message}`,
              ERRORS.PROFILE_CREATION_FAILED
            );
      }

      restoreStep = RESTORE_STEPS.RESTORE_RECOVER_RESOURCES;
      let postRecovery = await this.#recoverResources(
        manifest,
        recoveryPath,
        profile.path,
        wasEncrypted
      );

      if (copiedProfile) {
        let profileAge = await lazy.ProfileAge(profile.path);
        await profileAge.recordProfileCopied();
      }

      restoreStep = RESTORE_STEPS.RESTORE_CONFIGURE_PROFILE;
      let isLegacyBackup = manifest.meta?.isSelectableProfile === false;

      if (replaceCurrentProfile && isLegacyBackup) {
        // Legacy backup replacing a selectable profile: keep the current
        // profile's name, avatar, and theme instead of the backup's.
        let currentSelectableProfile =
          lazy.SelectableProfileService.currentProfile;

        profile.name = currentSelectableProfile.name;
        await profile.setAvatar(
          currentSelectableProfile.hasCustomAvatar
            ? await currentSelectableProfile.getAvatarFile()
            : currentSelectableProfile.avatar
        );

        let currentTheme = currentSelectableProfile.theme;
        await profile.setThemeAsync(currentTheme);

        // The backup's addon data has the legacy theme active, so schedule
        // enableTheme() via post-recovery to activate the correct one.
        postRecovery[
          DefaultBackupResources.SelectableProfileBackupResource.key
        ] = { themeId: currentTheme.themeId };
      } else if (!replaceCurrentProfile && isLegacyBackup) {
        // Adding a legacy backup as a new profile: use the backup's theme.
        let themeId = await this.#getLegacyThemeId(recoveryPath);
        let { themeFg, themeBg } =
          lazy.SelectableProfileService.getColorsForDefaultTheme();
        await profile.setThemeAsync({ themeId, themeFg, themeBg });

        // Schedule enableTheme() via post-recovery so that the correct
        // theme colors are computed from the recovered profile's window.
        postRecovery[
          DefaultBackupResources.SelectableProfileBackupResource.key
        ] = { themeId };
      }

      restoreStep = RESTORE_STEPS.RESTORE_WRITE_POST_RECOVERY;
      if (!copiedProfile) {
        try {
          postRecovery.backupServiceInternal = {
            isProfileRestore: true,
            restoreID: this.#_state.restoreID,
            backupMetadata: {
              date: this.#_state.backupFileInfo.date,
              appName: this.#_state.backupFileInfo.appName,
              appVersion: this.#_state.backupFileInfo.appVersion,
              buildID: this.#_state.backupFileInfo.buildID,
              osName: this.#_state.backupFileInfo.osName,
              osVersion: this.#_state.backupFileInfo.osVersion,
              legacyClientID: this.#_state.backupFileInfo.legacyClientID,
              healthTelemetryEnabled:
                this.#_state.backupFileInfo.healthTelemetryEnabled,
              intermediateProfileCreationDate:
                this.#_state.intermediateProfileCreationDate,
              restoreSource: this.#_state.restoreSource,
            },
          };
        } catch {}
      }

      await this.#writePostRecoveryData(postRecovery, profile.path);

      restoreStep = RESTORE_STEPS.RESTORE_LAUNCH_PROFILE;
      if (shouldLaunch) {
        // TODO (see Bug 2011302) - if the user is recovering a legacy profile
        // into a selectable profile, we should open a custom about:editprofile#recovered page

        lazy.SelectableProfileService.launchInstance(
          profile,
          // Using URL Search Params on this about: page didn't work because
          // the RPM communication so we use the hash and parse that instead.
          [
            "about:editprofile" +
              (copiedProfile
                ? `#copiedProfileName=${copiedProfile.name}`
                : "#restoredProfile"),
          ]
        );
      }

      return profile;
    } catch (e) {
      lazy.logConsole.error(
        "Failed to recover SelectableProfile from backup at ",
        recoveryPath,
        e
      );
      let err =
        e instanceof BackupError
          ? e
          : new BackupError(
              `Recovery failed: ${e.message}`,
              ERRORS.RECOVERY_FAILED
            );
      err.restoreStep = err.restoreStep || restoreStep;
      throw err;
    }
  }

  /**
   * Checks for the POST_RECOVERY_FILE_NAME in the current profile directory.
   * If one exists, instantiates any relevant BackupResource's, and calls
   * postRecovery() on them with the appropriate entry from the file. Once
   * this is done, deletes the file.
   *
   * The file is deleted even if one of the postRecovery() steps rejects or
   * fails.
   *
   * This function resolves silently if the POST_RECOVERY_FILE_NAME file does
   * not exist, which should be the majority of cases.
   *
   * @param {string} [profilePath=PathUtils.profileDir]
   *  The profile path to look for the POST_RECOVERY_FILE_NAME file. Defaults
   *  to the current profile.
   * @returns {Promise<undefined>}
   */
  async checkForPostRecovery(profilePath = PathUtils.profileDir) {
    lazy.logConsole.debug(`Checking for post-recovery file in ${profilePath}`);
    let postRecoveryFile = PathUtils.join(
      profilePath,
      BackupService.POST_RECOVERY_FILE_NAME
    );

    if (!(await IOUtils.exists(postRecoveryFile))) {
      lazy.logConsole.debug("Did not find post-recovery file.");
      this.#postRecoveryResolver();
      return;
    }

    lazy.logConsole.debug("Found post-recovery file. Loading...");

    try {
      let postRecovery = await IOUtils.readJSON(postRecoveryFile);
      for (let resourceKey in postRecovery) {
        let postRecoveryEntry = postRecovery[resourceKey];
        if (
          resourceKey == "backupServiceInternal" &&
          postRecoveryEntry.isProfileRestore
        ) {
          Services.prefs.setStringPref(
            RESTORED_BACKUP_METADATA_PREF_NAME,
            JSON.stringify(postRecoveryEntry.backupMetadata)
          );
          Glean.browserBackup.restoredProfileLaunched.record({
            restore_id: postRecoveryEntry.restoreID,
          });
          // This will clear out the data in this ping, which is a bit of a problem
          // for testing. So fire off an event first that tests can listen for.
          Services.obs.notifyObservers(
            null,
            "browser-backup-restored-profile-telemetry-set"
          );
          GleanPings.postProfileRestore.submit();
        } else {
          let resourceClass = this.#resources.get(resourceKey);
          if (!resourceClass) {
            lazy.logConsole.error(
              `Invalid resource for post-recovery step: ${resourceKey}`
            );
            continue;
          }

          lazy.logConsole.debug(
            `Running post-recovery step for ${resourceKey}`
          );
          try {
            await new resourceClass().postRecovery(postRecoveryEntry);
          } catch (e) {
            lazy.logConsole.error(
              `Post-recovery step for ${resourceKey} failed`,
              e
            );
          }
          lazy.logConsole.debug(`Done post-recovery step for ${resourceKey}`);
        }
      }
    } finally {
      await IOUtils.remove(postRecoveryFile, {
        ignoreAbsent: true,
        retryReadonly: true,
      });
      this.#postRecoveryResolver();
    }
  }

  /**
   * This local function reads and returns a desktop.ini file's contents,
   * allowing creation or verification of existing files.
   *
   * @param {string} LocalizedResourceName - translation of the folder name
   */
  #getDesktopIni(LocalizedResourceName) {
    return (
      `\r\n` +
      `[.ShellClassInfo]\r\n` +
      `LocalizedResourceName=${LocalizedResourceName}\r\n`
    );
  }

  /**
   * This function generates a `desktop.ini` file to translate a folder's name,
   * potentially laying the groundwork for future custom icon support. It also
   * configures Windows file pickers to recognize and display the new folder
   * name by adjusting both directory and file attributes accordingly.
   *
   * @param {string} fullPath directory path
   */
  async #createDesktopIni(fullPath) {
    let desktopIni = PathUtils.join(fullPath, "desktop.ini");
    try {
      lazy.logConsole.debug(`Creating desktop.ini file: ${desktopIni}`);
      await IOUtils.writeUTF8(
        desktopIni,
        this.#getDesktopIni(BackupService.BACKUP_DIR_TRANSLATION),
        { compress: false }
      );

      // set desktop file attributes to "system" and "hidden"
      await IOUtils.setWindowsAttributes(
        desktopIni,
        { system: true, hidden: true },
        false
      );

      // set folder attributes to "system"
      await IOUtils.setWindowsAttributes(fullPath, { system: true }, false);

      return true;
    } catch (e) {
      lazy.logConsole.warn(`Could not create ${desktopIni}: ${e}`);
    }
    return false;
  }

  /**
   * This function reverses the effects of createDesktopIni(...), removing the
   * `desktop.ini` file and resetting the folder's attributes. It will only
   * delete unmodified desktop.ini files and always return success when done.
   *
   * @param {string} fullPath - path where the desktop.ini can be found
   * @returns {Promise} - this promise always resolves, because a desktop.ini
   *                      is not considered to be very imporant.
   */
  async maybeCleanupDesktopIni(fullPath) {
    try {
      let desktopIni = PathUtils.join(fullPath, "desktop.ini");
      lazy.logConsole.debug(
        `Attempting to delete desktop.ini: '${desktopIni}'`
      );
      if (await IOUtils.exists(desktopIni)) {
        let expectedContents = this.#getDesktopIni(
          BackupService.BACKUP_DIR_TRANSLATION
        );

        // If the desktop.ini exists, its integrity is suspect because it may
        // have been tampered with. Checking its size first avoids potential
        // delays caused by large files; if the size does not match our
        // expected value, the file was altered and should be avoided for
        // further manipulation.
        let fileInfo = await IOUtils.stat(desktopIni);
        if (fileInfo && fileInfo.size == expectedContents.length) {
          // Now let us compare the content of a desktop.ini that we would
          // create today with the one on disk (could have been customized)
          let currentContents = await IOUtils.readUTF8(desktopIni);
          if (currentContents == expectedContents) {
            await IOUtils.remove(desktopIni, { retryReadonly: false });
          }
        } else {
          throw new BackupError(
            "The desktop.ini file has been modified and differs in size:" +
              ` ${fileInfo.size} != ${expectedContents.length}: ${desktopIni}`
          );
        }

        // Remove the system permission from the folder, which is set when the
        // `desktop.ini` file is created.
        await IOUtils.setWindowsAttributes(fullPath, { system: false }, false);
      }
    } catch (e) {
      lazy.logConsole.warn(
        `Unable to remove a desktop.ini file from ${fullPath}: ${e}`
      );
    }
  }

  /**
   * Sets the parent directory of the backups folder. Calling this function will update
   * browser.backup.location.
   *
   * @param {string} parentDirPath directory path
   */
  async setParentDirPath(parentDirPath) {
    try {
      let filename = parentDirPath ? PathUtils.filename(parentDirPath) : null;
      if (!filename) {
        throw new BackupError(
          "Parent directory path is invalid.",
          ERRORS.FILE_SYSTEM_ERROR
        );
      }

      let fullPath = parentDirPath;
      if (filename != BackupService.BACKUP_DIR_NAME) {
        // Recreate the backups path with the new parent directory.
        fullPath = PathUtils.join(parentDirPath, BackupService.BACKUP_DIR_NAME);
      }

      Services.prefs.setStringPref(BACKUP_DIR_PREF_NAME, fullPath);
    } catch (e) {
      lazy.logConsole.error(
        `Failed to set parent directory ${parentDirPath}. ${e}`
      );
      throw e;
    }
  }

  /**
   * Updates backupDirPath in the backup service state. Should be called every time the value
   * for browser.backup.location changes.
   *
   * @param {string} newDirPath the new directory path for storing backups
   */
  async onUpdateLocationDirPath(newDirPath) {
    lazy.logConsole.debug(`Updating backup location to ${newDirPath}`);

    Glean.browserBackup.changeLocation.record();

    this.#_state.backupDirPath = newDirPath;
    this.stateUpdate();
  }

  /**
   * Updates backupErrorCode in the backup service state. Should be called every time
   * the value for browser.backup.errorCode changes.
   *
   * @param {number} newErrorCode
   *    Any of the ERROR code's from backup-constants.mjs
   */
  onUpdateBackupErrorCode(newErrorCode) {
    lazy.logConsole.debug(`Updating backup error code to ${newErrorCode}`);

    this.#_state.backupErrorCode = newErrorCode;
    this.stateUpdate();
  }

  /**
   * Updates lastBackupFileName in the backup service state. Should be called every time
   * the value for browser.backup.scheduled.last-backup-file changes.
   *
   * @param {string} newLastBackupFileName
   *    Name of the last known backup file
   */
  onUpdateLastBackupFileName(newLastBackupFileName) {
    lazy.logConsole.debug(
      `The last backup file name is being updated to ${newLastBackupFileName}`
    );

    this.#_state.lastBackupFileName = newLastBackupFileName;

    if (!newLastBackupFileName) {
      lazy.logConsole.debug(
        `Looks like we've cleared the last backup file name, let's also clear the last backup date`
      );

      this.#_state.lastBackupDate = null;
      Services.prefs.clearUserPref(LAST_BACKUP_TIMESTAMP_PREF_NAME);
    }

    this.stateUpdate();
  }

  /**
   * Updates selectableProfilesAllowed in the backup service state. Should be called every time
   * the SelectableProfileService enabled state is changed.
   *
   */
  onUpdateProfilesEnabledState() {
    lazy.logConsole.debug(
      `The profiles enabled state was updated to ${lazy.SelectableProfileService.isEnabled}`
    );

    this.#_state.selectableProfilesAllowed =
      lazy.SelectableProfileService.isEnabled;
    this.stateUpdate();
  }

  /**
   * Returns the moz-icon URL of a file. To get the moz-icon URL, the
   * file path is convered to a fileURI. If there is a problem retreiving
   * the moz-icon due to an invalid file path, return null instead.
   *
   * @param {string} path Path of the file to read its icon from.
   * @returns {string|null} The moz-icon URL of the specified file, or
   *  null if the icon cannot be retreived.
   */
  getIconFromFilePath(path) {
    if (!path) {
      return null;
    }

    try {
      let fileURI = PathUtils.toFileURI(path);
      return `moz-icon:${fileURI}?size=16`;
    } catch (e) {
      return null;
    }
  }

  /**
   * Sets browser.backup.scheduled.enabled to true or false.
   *
   * @param { boolean } shouldEnableScheduledBackups true if scheduled backups should be enabled. Else, false.
   * @param { string } [source] Identifies the UI that is toggling scheduled
   * backups, in either direction. "preferences" is used for the main backup
   * settings page. If toggled by a message, use the message ID. Recorded to
   * the scheduler_toggle_source metric, paired with scheduler_enabled.
   */
  setScheduledBackups(shouldEnableScheduledBackups, source = "unknown") {
    this.#scheduledBackupsToggleSource = source || "unknown";

    Services.prefs.setBoolPref(
      SCHEDULED_BACKUPS_ENABLED_PREF_NAME,
      shouldEnableScheduledBackups
    );

    if (shouldEnableScheduledBackups) {
      // reset the error states when reenabling backup
      Services.prefs.setIntPref(BACKUP_ERROR_CODE_PREF_NAME, ERRORS.NONE);

      // flush the embedded component's persistent data
      this.setEmbeddedComponentPersistentData({});

      BackupService.maybeAddToEnabledListPref();
    } else {
      // set user-disabled pref if backup is being disabled
      Services.prefs.setBoolPref(
        "browser.backup.scheduled.user-disabled",
        true
      );

      BackupService.maybeRemoveFromEnabledListPref();
    }
  }

  /**
   * Updates scheduledBackupsEnabled in the backup service state. Should be called every time
   * the value for browser.backup.scheduled.enabled changes.
   *
   * @param {boolean} isScheduledBackupsEnabled True if scheduled backups are enabled. Else false.
   */
  onUpdateScheduledBackups(isScheduledBackupsEnabled) {
    if (this.#_state.scheduledBackupsEnabled != isScheduledBackupsEnabled) {
      if (isScheduledBackupsEnabled) {
        Glean.browserBackup.toggleOn.record({
          encrypted: this.#_state.encryptionEnabled,
          location: this.classifyLocationForTelemetry(lazy.backupDirPref),
        });
      } else {
        Glean.browserBackup.toggleOff.record();
      }
      Glean.browserBackup.schedulerToggleSource.set(
        this.#scheduledBackupsToggleSource
      );
      // Reset the source to "unknown" so a subsequent toggle does not inherit
      // a stale source.
      this.#scheduledBackupsToggleSource = "unknown";

      lazy.logConsole.debug(
        "Updating scheduled backups",
        isScheduledBackupsEnabled
      );
      this.#_state.scheduledBackupsEnabled = isScheduledBackupsEnabled;
      this.stateUpdate();
    }
  }

  /**
   * Take measurements of the current profile state for Telemetry.
   *
   * @returns {Promise<undefined>}
   */
  async takeMeasurements() {
    lazy.logConsole.debug("Taking Telemetry measurements");

    // We'll start by taking some basic BackupService state measurements.
    Glean.browserBackup.enabled.set(true);
    Glean.browserBackup.schedulerEnabled.set(lazy.scheduledBackupsPref);

    await this.loadEncryptionState();
    Glean.browserBackup.pswdEncrypted.set(this.#_state.encryptionEnabled);

    // No default location to compare against, so leave locationOnDevice unset.
    let defaultParentDirPath = BackupService.DEFAULT_PARENT_DIR_PATH;
    if (defaultParentDirPath) {
      const USING_DEFAULT_DIR_PATH =
        lazy.backupDirPref ==
        PathUtils.join(defaultParentDirPath, BackupService.BACKUP_DIR_NAME);
      Glean.browserBackup.locationOnDevice.set(USING_DEFAULT_DIR_PATH ? 1 : 2);
    }

    // Next, we'll measure the available disk space on the storage
    // device that the profile directory is on.
    let profileDir = await IOUtils.getFile(PathUtils.profileDir);

    let profDDiskSpaceBytes = profileDir.diskSpaceAvailable;

    // Make the measurement fuzzier by rounding to the nearest 10MB.
    let profDDiskSpaceFuzzed = MeasurementUtils.fuzzByteSize(
      profDDiskSpaceBytes,
      10 * BYTES_IN_MEGABYTE
    );

    // And then record the value in kilobytes, since that's what everything
    // else is going to be measured in.
    Glean.browserBackup.profDDiskSpace.set(
      profDDiskSpaceFuzzed / BYTES_IN_KILOBYTE
    );

    // Measure the size of each file we are going to backup.
    for (let resourceClass of this.#resources.values()) {
      try {
        await new resourceClass().measure(PathUtils.profileDir);
      } catch (e) {
        lazy.logConsole.error(
          `Failed to measure for resource: ${resourceClass.key}`,
          e
        );
      }
    }
  }

  /**
   * The internal promise that is created on the first call to
   * loadEncryptionState.
   *
   * @type {Promise}
   */
  #loadEncryptionStatePromise = null;

  /**
   * Returns the current ArchiveEncryptionState. This method will only attempt
   * to read the state from the disk the first time it is called.
   *
   * @param {string} [profilePath=PathUtils.profileDir]
   *   The profile path where the encryption state might exist. This is only
   *   used for testing.
   * @returns {Promise<ArchiveEncryptionState>}
   */
  loadEncryptionState(profilePath = PathUtils.profileDir) {
    if (this.#encState !== undefined) {
      return Promise.resolve(this.#encState);
    }

    // This little dance makes it so that we only attempt to read the state off
    // of the disk the first time `loadEncryptionState` is called. Any
    // subsequent calls will await this same promise, OR, after the state has
    // been read in, they'll just get the #encState which is set after the
    // state has been read in.
    if (!this.#loadEncryptionStatePromise) {
      this.#loadEncryptionStatePromise = (async () => {
        // Default this to null here - that way, if we fail to read it in,
        // the null will indicate that we have at least _tried_ to load the
        // state.
        let encState = null;
        let encStateFile = PathUtils.join(
          profilePath,
          BackupService.PROFILE_FOLDER_NAME,
          BackupService.ARCHIVE_ENCRYPTION_STATE_FILE
        );

        // Try to read in any pre-existing encryption state. If that fails,
        // we fallback to not encrypting, and only backing up non-sensitive data.
        try {
          if (await IOUtils.exists(encStateFile)) {
            let stateObject = await IOUtils.readJSON(encStateFile);
            ({ instance: encState } =
              await lazy.ArchiveEncryptionState.initialize(stateObject));
          }
        } catch (e) {
          lazy.logConsole.error(
            "Failed to read / deserialize archive encryption state file: ",
            e
          );
          // TODO: This kind of error might be worth collecting telemetry on.
        }

        this.#_state.encryptionEnabled = !!encState;
        this.stateUpdate();

        this.#encState = encState;
        return encState;
      })();
    }

    return this.#loadEncryptionStatePromise;
  }

  /**
   * Enables encryption for backups, allowing sensitive data to be backed up.
   * After enabling encryption, the state is written to disk.
   *
   * @throws Exception
   * @param {string} password
   *   A non-blank password ("recovery code") that can be used to derive keys
   *   for encrypting the backup.
   * @param {string} [profilePath=PathUtils.profileDir]
   *   The profile path where the encryption state will be written. This is only
   *   used for testing.
   */
  async enableEncryption(password, profilePath = PathUtils.profileDir) {
    lazy.logConsole.debug("Enabling encryption.");
    if (!password) {
      throw new BackupError(
        "Cannot supply a blank password.",
        ERRORS.INVALID_PASSWORD
      );
    }

    if (password.length < 8) {
      throw new BackupError(
        "Password must be at least 8 characters.",
        ERRORS.INVALID_PASSWORD
      );
    }

    let { instance: encState } =
      await lazy.ArchiveEncryptionState.initialize(password);
    if (!encState) {
      throw new BackupError(
        "Failed to construct ArchiveEncryptionState",
        ERRORS.UNKNOWN
      );
    }

    this.#encState = encState;

    let encStateFile = PathUtils.join(
      profilePath,
      BackupService.PROFILE_FOLDER_NAME,
      BackupService.ARCHIVE_ENCRYPTION_STATE_FILE
    );

    let stateObj = await encState.serialize();
    await IOUtils.writeJSON(encStateFile, stateObj);

    this.#_state.encryptionEnabled = true;
    this.stateUpdate();
  }

  /**
   * Disables encryption of backups.
   *
   * @throws Exception
   * @param {string} [profilePath=PathUtils.profileDir]
   *   The profile path where the encryption state exists. This is only used for
   *   testing.
   * @returns {Promise<undefined>}
   */
  async disableEncryption(profilePath = PathUtils.profileDir) {
    lazy.logConsole.debug("Disabling encryption.");
    let encStateFile = PathUtils.join(
      profilePath,
      BackupService.PROFILE_FOLDER_NAME,
      BackupService.ARCHIVE_ENCRYPTION_STATE_FILE
    );
    await IOUtils.remove(encStateFile, {
      ignoreAbsent: true,
      retryReadonly: true,
    });

    this.#encState = null;
    this.#_state.encryptionEnabled = false;
    this.stateUpdate();
  }

  /**
   * The value of IDLE_THRESHOLD_SECONDS_PREF_NAME at the time that
   * initBackupScheduler was called. This is recorded so that if the preference
   * changes at runtime, that we properly remove the idle observer in
   * uninitBackupScheduler, since it's mapped to the idle time value.
   *
   * @see BackupService.initBackupScheduler()
   * @see BackupService.uninitBackupScheduler()
   * @type {number}
   */
  #idleThresholdSeconds = null;

  /**
   * An ES6 class that extends EventTarget cannot, apparently, be coerced into
   * a nsIObserver, even when we define QueryInterface. We work around this
   * limitation by having the observer be a function that we define at
   * registration time. We hold a reference to the observer so that we can
   * properly unregister.
   *
   * @see BackupService.initBackupScheduler()
   * @type {Function}
   */
  #observer = null;

  /**
   * True if the backup scheduler system has been initted via
   * initBackupScheduler().
   *
   * @see BackupService.initBackupScheduler()
   * @type {boolean}
   */
  #backupSchedulerInitted = false;

  /**
   * Initializes the backup scheduling system. This should be done shortly
   * after startup. It is exposed as a public method mainly for ease in testing.
   *
   * The scheduler will automatically uninitialize itself on the
   * quit-application-granted observer notification.
   */
  initBackupScheduler() {
    if (this.#backupSchedulerInitted) {
      lazy.logConsole.warn(
        "BackupService scheduler already initting or initted."
      );
      return;
    }

    this.#backupSchedulerInitted = true;

    let lastBackupPrefValue = Services.prefs.getIntPref(
      LAST_BACKUP_TIMESTAMP_PREF_NAME,
      0
    );

    this.#_state.lastBackupDate = lastBackupPrefValue || null;

    this.stateUpdate();

    // We'll default to 5 minutes of idle time unless otherwise configured.
    const FIVE_MINUTES_IN_SECONDS = 5 * 60;

    this.#idleThresholdSeconds = Services.prefs.getIntPref(
      IDLE_THRESHOLD_SECONDS_PREF_NAME,
      FIVE_MINUTES_IN_SECONDS
    );
    this.#observer = (subject, topic, data) => {
      this.onObserve(subject, topic, data);
    };
    lazy.logConsole.debug(
      `Registering idle observer for ${
        this.#idleThresholdSeconds
      } seconds of idle time`
    );
    lazy.idleService.addIdleObserver(
      this.#observer,
      this.#idleThresholdSeconds
    );
    lazy.logConsole.debug("Idle observer registered.");

    lazy.logConsole.debug(`Registering Places observer`);

    this.#placesObserver = new PlacesWeakCallbackWrapper(
      this.onPlacesEvents.bind(this)
    );
    PlacesObservers.addListener(
      ["history-cleared", "page-removed", "bookmark-removed"],
      this.#placesObserver
    );

    lazy.AddonManager.addAddonListener(this);

    Services.obs.addObserver(this.#observer, "passwordmgr-storage-changed");
    Services.obs.addObserver(this.#observer, "formautofill-storage-changed");
    Services.obs.addObserver(this.#observer, "sanitizer-sanitization-complete");
    Services.obs.addObserver(this.#observer, "perm-changed");
    Services.obs.addObserver(this.#observer, "cookie-changed");
    Services.obs.addObserver(this.#observer, "session-cookie-changed");
    Services.obs.addObserver(this.#observer, "newtab-linkBlocked");
    Services.obs.addObserver(this.#observer, "quit-application-granted");
    Services.prefs.addObserver(SANITIZE_ON_SHUTDOWN_PREF_NAME, this.#observer);
  }

  /**
   * Uninitializes the backup scheduling system.
   */
  uninitBackupScheduler() {
    if (!this.#backupSchedulerInitted) {
      lazy.logConsole.warn(
        "Tried to uninitBackupScheduler when it wasn't yet enabled."
      );
      return;
    }

    lazy.idleService.removeIdleObserver(
      this.#observer,
      this.#idleThresholdSeconds
    );

    PlacesObservers.removeListener(
      ["history-cleared", "page-removed", "bookmark-removed"],
      this.#placesObserver
    );

    lazy.AddonManager.removeAddonListener(this);

    Services.obs.removeObserver(this.#observer, "passwordmgr-storage-changed");
    Services.obs.removeObserver(this.#observer, "formautofill-storage-changed");
    Services.obs.removeObserver(
      this.#observer,
      "sanitizer-sanitization-complete"
    );
    Services.obs.removeObserver(this.#observer, "perm-changed");
    Services.obs.removeObserver(this.#observer, "cookie-changed");
    Services.obs.removeObserver(this.#observer, "session-cookie-changed");
    Services.obs.removeObserver(this.#observer, "newtab-linkBlocked");
    Services.obs.removeObserver(this.#observer, "quit-application-granted");
    Services.prefs.removeObserver(
      SANITIZE_ON_SHUTDOWN_PREF_NAME,
      this.#observer
    );
    this.#observer = null;

    this.#regenerationDebouncer.disarm();
    this.#backupWriteAbortController.abort();
  }

  /**
   * Called by this.#observer on idle from the nsIUserIdleService or
   * quit-application-granted from the nsIObserverService. Exposed as a public
   * method mainly for ease in testing.
   *
   * @param {nsISupports|null} subject
   *   The nsIUserIdleService for the idle notification, and null for the
   *   quit-application-granted topic.
   * @param {string} topic
   *   The topic that the notification belongs to.
   * @param {string} data
   *   Optional data that was included with the notification.
   */
  onObserve(subject, topic, data) {
    switch (topic) {
      case "idle": {
        this.onIdle();
        break;
      }
      case "quit-application-granted": {
        this.uninitBackupScheduler();
        this.uninitStatusObservers();
        break;
      }
      case "passwordmgr-storage-changed": {
        if (data == "removeLogin" || data == "removeAllLogins") {
          this.#debounceRegeneration();
        }
        break;
      }
      case "formautofill-storage-changed": {
        if (
          data == "remove" &&
          (subject.wrappedJSObject.collectionName == "creditCards" ||
            subject.wrappedJSObject.collectionName == "addresses")
        ) {
          this.#debounceRegeneration();
        }
        break;
      }
      case "newtab-linkBlocked":
      // Intentional fall-through
      case "sanitizer-sanitization-complete": {
        this.#debounceRegeneration();
        break;
      }
      case "perm-changed": {
        if (data == "deleted") {
          this.#debounceRegeneration();
        }
        break;
      }
      case "cookie-changed":
      // Intentional fall-through
      case "session-cookie-changed": {
        let notification = subject.QueryInterface(Ci.nsICookieNotification);
        // A browsingContextId value of 0 means that this deletion was caused by
        // chrome UI cookie deletion, which is what we care about. If it's not
        // 0, then a site deleted its own cookie, which we ignore.
        if (
          (notification.action == Ci.nsICookieNotification.COOKIE_DELETED ||
            notification.action ==
              Ci.nsICookieNotification.ALL_COOKIES_CLEARED) &&
          !notification.browsingContextId
        ) {
          this.#debounceRegeneration();
        }
        break;
      }
      case "nsPref:changed": {
        if (data == SANITIZE_ON_SHUTDOWN_PREF_NAME) {
          this.#debounceRegeneration();
        }
      }
    }
  }

  /**
   * Makes this instance responsible for monitoring the conditions that can
   * cause backups or restores to be unavailable.
   *
   * When one arrives, observers of the 'backup-service-status-changed' topic
   * will be notified and telemetry will be emitted.
   *
   * This is not done by default since that would cause N emissions of that
   * topic per change for N instances, which can be a problem with testing. The
   * global BackupService has status observers by default.
   */
  initStatusObservers() {
    if (this.#statusPrefObserver != null) {
      return;
    }

    // We don't use this.#observer since any changes to the prefs or nimbus should
    // immediately reflect across any observers, instead of waiting on idle.
    this.#statusPrefObserver = () => {
      // Wrap in an arrow function so 'this' is preserved.
      this.#handleStatusChange();
    };

    for (let pref of BackupService.STATUS_OBSERVER_PREFS) {
      Services.prefs.addObserver(pref, this.#statusPrefObserver);
    }
    lazy.NimbusFeatures.backupService.onUpdate(this.#statusPrefObserver);
    this.#handleStatusChange();

    this.#profileServiceStateObserver = () =>
      this.onUpdateProfilesEnabledState();
    lazy.SelectableProfileService.on(
      "enableChanged",
      this.#profileServiceStateObserver
    );
  }

  /**
   * Removes the observers configured by initStatusObservers.
   *
   * This is done automatically on shutdown, but you can do it earlier if you'd
   * like that instance to stop emitting events.
   */
  uninitStatusObservers() {
    if (this.#statusPrefObserver == null) {
      return;
    }

    for (let pref of BackupService.STATUS_OBSERVER_PREFS) {
      Services.prefs.removeObserver(pref, this.#statusPrefObserver);
    }
    lazy.NimbusFeatures.backupService.offUpdate(this.#statusPrefObserver);
    this.#statusPrefObserver = null;

    lazy.SelectableProfileService.off(
      "enableChanged",
      this.#profileServiceStateObserver
    );
    this.#profileServiceStateObserver = null;
  }

  /**
   * Performs tasks required whenever archive or restore change their status
   *
   * 1. Notifies any observers that a change has taken place
   * 2. If archive is disabled, clean up any backup files
   */
  #handleStatusChange() {
    const archiveStatus = this.archiveEnabledStatus;
    const restoreStatus = this.restoreEnabledStatus;
    // Update the BackupService state before notifying observers about the
    // state change
    this.#_state.archiveEnabledStatus = this.archiveEnabledStatus.enabled;
    this.#_state.restoreEnabledStatus = this.restoreEnabledStatus.enabled;

    this.#updateGleanEnablement(archiveStatus, restoreStatus);
    if (
      archiveStatus.enabled != this.#lastSeenArchiveStatus ||
      restoreStatus.enabled != this.#lastSeenRestoreStatus
    ) {
      this.#lastSeenArchiveStatus = archiveStatus.enabled;
      this.#lastSeenRestoreStatus = restoreStatus.enabled;
      this.#notifyStatusObservers();
    }
    if (!archiveStatus.enabled) {
      // We won't wait for this promise to accept/reject since rejections are
      // ignored anyways
      this.cleanupBackupFiles();
    }
  }

  #updateGleanEnablement(archiveStatus, restoreStatus) {
    Glean.browserBackup.archiveEnabled.set(archiveStatus.enabled);
    Glean.browserBackup.restoreEnabled.set(restoreStatus.enabled);
    if (!archiveStatus.enabled) {
      this.#wasArchivePreviouslyDisabled = true;
      Glean.browserBackup.archiveDisabledReason.set(
        archiveStatus.internalReason
      );
    } else if (this.#wasArchivePreviouslyDisabled) {
      Glean.browserBackup.archiveDisabledReason.set("reenabled");
    }
    if (!restoreStatus.enabled) {
      this.#wasRestorePreviouslyDisabled = true;
      Glean.browserBackup.restoreDisabledReason.set(
        restoreStatus.internalReason
      );
    } else if (this.#wasRestorePreviouslyDisabled) {
      Glean.browserBackup.restoreDisabledReason.set("reenabled");
    }
  }

  /**
   * Notify any listeners about the availability of the backup service, then
   * update relevant telemetry metrics.
   */
  #notifyStatusObservers() {
    lazy.logConsole.log(
      "Notifying observers about a BackupService state change"
    );

    Services.obs.notifyObservers(null, "backup-service-status-updated");
  }

  async cleanupBackupFiles() {
    lazy.logConsole.debug("Cleaning up backup data");
    try {
      if (this.state.encryptionEnabled) {
        await this.disableEncryption();
      }
      await this.deleteLastBackup();
    } catch (e) {
      // Ignore any exceptions
      lazy.logConsole.error(
        "There was an error when cleaning up backup files: ",
        e
      );
    }
  }

  /**
   * Called when the last known backup should be deleted and a new one
   * created. This uses the #regenerationDebouncer to debounce clusters of
   * events that might cause such a regeneration to occur.
   */
  #debounceRegeneration() {
    this.#regenerationDebouncer.disarm();
    this.#regenerationDebouncer.arm();
  }

  /**
   * Called when the nsIUserIdleService reports that user input events have
   * not been sent to the application for at least
   * IDLE_THRESHOLD_SECONDS_PREF_NAME seconds.
   */
  async onIdle() {
    lazy.logConsole.debug("Saw idle callback");
    if (!this.#takenMeasurements) {
      // Measurement failures should not block other onIdle backup activity.
      this.takeMeasurements().catch(e =>
        lazy.logConsole.error("Failed to take Telemetry measurements", e)
      );
      this.#takenMeasurements = true;
    }

    if (lazy.scheduledBackupsPref && this.archiveEnabledStatus.enabled) {
      lazy.logConsole.debug("Scheduled backups enabled.");
      let now = Math.floor(Date.now() / 1000);
      let lastBackupDate = this.#_state.lastBackupDate;
      if (lastBackupDate && lastBackupDate > now) {
        lazy.logConsole.error(
          "Last backup was somehow in the future. Resetting the preference."
        );
        lastBackupDate = null;
        this.#_state.lastBackupDate = null;
        this.stateUpdate();
      }

      if (!lastBackupDate) {
        lazy.logConsole.debug("No last backup time recorded in prefs.");
      } else {
        lazy.logConsole.debug(
          "Last backup was: ",
          new Date(lastBackupDate * 1000)
        );
      }

      if (
        !lastBackupDate ||
        now - lastBackupDate > lazy.minimumTimeBetweenBackupsSeconds
      ) {
        lazy.logConsole.debug(
          "Last backup exceeded minimum time between backups. Queueing a " +
            "backup via idleDispatch."
        );

        // Just because the user hasn't sent us events in a while doesn't mean
        // that the browser itself isn't busy. It might be, for example, playing
        // video or doing a complex calculation that the user is actively
        // waiting to complete, and we don't want to draw resources from that.
        // Instead, we'll use ChromeUtils.idleDispatch to wait until the event
        // loop in the parent process isn't so busy with higher priority things.
        let expectedBackupTime =
          lastBackupDate + lazy.minimumTimeBetweenBackupsSeconds;
        try {
          await this.createBackupOnIdleDispatch({
            reason:
              expectedBackupTime < this._startupTimeUnixSeconds
                ? "missed"
                : "idle",
          });
        } catch (e) {
          lazy.logConsole.error(
            "createBackupOnIdleDispatch promise rejected",
            e
          );
        }
      } else {
        lazy.logConsole.debug(
          "Last backup was too recent. Not creating one for now."
        );
      }
    }
  }

  /**
   * Gets the time that Firefox started as milliseconds since the Unix epoch.
   *
   * This is in a getter to make it easier for tests to stub it out.
   */
  get _startupTimeUnixSeconds() {
    let startupTimeMs = Services.startup.getStartupInfo().process.getTime();
    return Math.floor(startupTimeMs / 1000);
  }

  /**
   * Decide whether we should attempt a backup now.
   *
   * @returns {boolean}
   */
  shouldAttemptBackup() {
    let now = Math.floor(Date.now() / 1000);
    const debugInfoStr = Services.prefs.getStringPref(
      BACKUP_DEBUG_INFO_PREF_NAME,
      ""
    );

    let parsed = null;
    if (debugInfoStr) {
      try {
        parsed = JSON.parse(debugInfoStr);
      } catch (e) {
        lazy.logConsole.warn(
          "Invalid backup debug-info pref; ignoring and allowing backup attempt.",
          e
        );
        parsed = null;
      }
    }

    const lastBackupAttempt = parsed?.lastBackupAttempt;
    const hasErroredLastAttempt = Number.isFinite(lastBackupAttempt);

    if (!hasErroredLastAttempt) {
      lazy.logConsole.debug(
        `There have been no errored last attempts, let's do a backup`
      );
      return true;
    }

    const secondsSinceLastAttempt = now - lastBackupAttempt;

    if (lazy.isRetryDisabledOnIdle) {
      // Let's add a buffer before restarting the retries. Dividing by 2
      // since currently minimumTimeBetweenBackupsSeconds is set to 24 hours
      // We want to approximately keep a backup for each day, so let's retry
      // in about 12 hours again.
      if (secondsSinceLastAttempt < lazy.minimumTimeBetweenBackupsSeconds / 2) {
        lazy.logConsole.debug(
          `Retrying is disabled, we have to wait for ${lazy.minimumTimeBetweenBackupsSeconds / 2}s to retry`
        );
        return false;
      }
      // Looks like we've waited enough, reset the retry states and try to create
      // a backup again.
      BackupService.#errorRetries = 0;
      Services.prefs.clearUserPref(DISABLED_ON_IDLE_RETRY_PREF_NAME);

      return true;
    }

    // Exponential backoff guard, avoids throttling the same error again and again
    if (secondsSinceLastAttempt < BackupService.backoffSeconds()) {
      lazy.logConsole.debug(
        `backoff: elapsed ${secondsSinceLastAttempt}s < backoff ${BackupService.backoffSeconds()}s`
      );
      return false;
    }

    return true;
  }

  /**
   * Calls BackupService.createBackup at the next moment when the event queue
   * is not busy with higher priority events. This is intentionally broken out
   * into its own method to make it easier to stub out in tests.
   *
   * @param {object} [options]
   * @param {boolean} [options.deletePreviousBackup]
   * @param {string} [options.reason]
   *
   * @returns {Promise} A backup promise to hold onto
   */
  createBackupOnIdleDispatch({ deletePreviousBackup = true, reason }) {
    if (!this.shouldAttemptBackup()) {
      return Promise.resolve();
    }

    // Determine path to old backup file
    const oldBackupFile = this.#_state.lastBackupFileName;
    const isScheduledBackupsEnabled = lazy.scheduledBackupsPref;

    let { promise, resolve } = Promise.withResolvers();
    ChromeUtils.idleDispatch(async () => {
      lazy.logConsole.debug(
        "idleDispatch fired. Attempting to create a backup."
      );
      let oldBackupFilePath;
      if (await this.#infalliblePathExists(lazy.backupDirPref)) {
        oldBackupFilePath = PathUtils.join(lazy.backupDirPref, oldBackupFile);
      }

      let possibleArchivePath = "";

      try {
        if (isScheduledBackupsEnabled) {
          ({ archivePath: possibleArchivePath } = await this.createBackup({
            reason,
          }));
        }
      } catch (e) {
        lazy.logConsole.debug(
          `There was an error creating backup on idle dispatch: ${e}`
        );

        BackupService.#errorRetries += 1;
        if (BackupService.#errorRetries > lazy.backupRetryLimit) {
          Services.prefs.setBoolPref(DISABLED_ON_IDLE_RETRY_PREF_NAME, true);
          // Next retry will be 24 hours later (backoffSeconds = 2^(11) * 60s),
          // let's just restart our backoff heuristic
          BackupService.#errorRetries = 0;
          Glean.browserBackup.backupThrottled.record();
        }
      } finally {
        // Now delete the old backup file, if it exists
        if (
          deletePreviousBackup &&
          oldBackupFilePath &&
          oldBackupFilePath != possibleArchivePath
        ) {
          lazy.logConsole.log(
            "Attempting to delete last backup file at ",
            oldBackupFilePath
          );
          await this.maybeCleanupDesktopIni(lazy.backupDirPref);
          await IOUtils.remove(oldBackupFilePath, {
            ignoreAbsent: true,
            retryReadonly: true,
          });
        }
        resolve();
      }
    });
    return promise;
  }

  /**
   * Handler for events coming in through our PlacesObserver.
   *
   * @param {PlacesEvent[]} placesEvents
   *   One or more batched events that are of a type that we subscribed to.
   */
  onPlacesEvents(placesEvents) {
    // Note that if any of the events that we iterate result in a regeneration
    // being queued, we simply return without the processing the rest, as there
    // is not really a point.
    for (let event of placesEvents) {
      switch (event.type) {
        case "page-removed": {
          // We will get a page-removed event if a page has been deleted both
          // manually by a user, but also automatically if the page has "aged
          // out" of the Places database. We only want to regenerate backups
          // in the manual case (REASON_DELETED).
          if (event.reason == PlacesVisitRemoved.REASON_DELETED) {
            this.#debounceRegeneration();
            return;
          }
          break;
        }
        case "bookmark-removed":
        // Intentional fall-through
        case "history-cleared": {
          this.#debounceRegeneration();
          return;
        }
      }
    }
  }

  /**
   * This method is the only method of the AddonListener interface that
   * BackupService implements and is called by AddonManager when an addon
   * is uninstalled.
   *
   * @param {AddonInternal} _addon
   *   The addon being uninstalled.
   */
  onUninstalled(_addon) {
    this.#debounceRegeneration();
  }

  /**
   * Sets the backup file path to restore from and updates state.
   *
   * @param {string} backupFilePath path to the backup file.
   */
  setBackupFileToRestore(backupFilePath) {
    this.#_state.backupFileToRestore = backupFilePath;
    this.stateUpdate();
  }

  /**
   * Gets a sample from a given backup file and sets a subset of that as
   * the backupFileInfo in the backup service state.
   *
   * Called when loading info for an archive to potentially restore.
   *
   * @param {string} backupFilePath path to the backup file to sample.
   */
  async loadBackupFileInfo(backupFilePath) {
    lazy.logConsole.debug(`Getting info from backup file at ${backupFilePath}`);

    this.#_state.restoreID = Services.uuid.generateUUID().toString();
    this.#_state.backupFileInfo = null;
    this.#_state.backupFileCoarseLocation =
      this.classifyLocationForTelemetry(backupFilePath);

    try {
      let { archiveJSON, isEncrypted } =
        await this.sampleArchive(backupFilePath);
      this.#_state.backupFileInfo = {
        isEncrypted,
        date: archiveJSON?.meta?.date,
        deviceName: archiveJSON?.meta?.deviceName,
        appName: archiveJSON?.meta?.appName,
        appVersion: archiveJSON?.meta?.appVersion,
        buildID: archiveJSON?.meta?.buildID,
        osName: archiveJSON?.meta?.osName,
        osVersion: archiveJSON?.meta?.osVersion,
        osBuildNumber: archiveJSON?.meta?.osBuildNumber,
        healthTelemetryEnabled: archiveJSON?.meta?.healthTelemetryEnabled,
        legacyClientID: archiveJSON?.meta?.legacyClientID,
        profileName: archiveJSON?.meta?.profileName,
      };

      // Clear any existing recovery error from state since we've successfully
      // got our file info. Make sure to do this last, since it will cause
      // state change observers to fire.
      this.setRecoveryError(ERRORS.NONE);
    } catch (error) {
      // If the file is invalid, then null out the info. Keep
      // backupFileToRestore and backupFileCoarseLocation, though, to avoid
      // blanking out the input.
      this.#_state.backupFileInfo = null;

      // Notify observers of the error last, after we have set the state.
      this.setRecoveryError(error.cause);
    }
  }

  /**
   * TEST ONLY: reset's lastBackup state's for testing purposes
   */
  resetLastBackupInternalState() {
    this.#_state.backupFileToRestore = null;
    this.#_state.backupFileInfo = null;
    this.#_state.lastBackupFileName = "";
    this.#_state.lastBackupDate = null;
    this.stateUpdate();
  }

  /**
   * TEST ONLY: reset's the defaultParent state for testing purposes
   */
  resetDefaultParentInternalState() {
    this.#_state.defaultParent = {};
    this.stateUpdate();
  }

  /*
   * Attempts to open a native file explorer window at the last backup file's
   * location on the filesystem.
   */
  async showBackupLocation() {
    let backupFilePath = PathUtils.join(
      lazy.backupDirPref,
      lazy.lastBackupFileName
    );
    if (await IOUtils.exists(backupFilePath)) {
      new lazy.nsLocalFile(backupFilePath).reveal();
    } else {
      let archiveDestFolderPath = await this.resolveArchiveDestFolderPath(
        lazy.backupDirPref
      );
      new lazy.nsLocalFile(archiveDestFolderPath).reveal();
    }
  }

  /**
   * Searches for a valid backup file in the default backup folder.
   *
   * This function checks the possible backup directory's for `.html` backup files.
   * If multiple backups are present and `multipleFiles` is false, it will not select one.
   * Optionally validates each candidate file before selecting it.
   *
   * @param {object} [options={}] - Configuration options.
   * @param {boolean} [options.validateFile=true] - Whether to validate each backup file before selecting it.
   * @param {boolean} [options.multipleFiles=false] - Whether to allow selecting when multiple backup files are found.
   * @param {boolean} [options.speedUpHeuristic=false] - Whether we want to avoid performance bottlenecks in exchange for
   *                              possibly missing valid files.
   *
   * @returns {Promise<object>} A result object with the following properties:
   * - {boolean} multipleBackupsFound — True if more than one backup candidate was found and `multipleFiles` is false.
   * - {number} count — The number of backup candidate files found matching the expected filename pattern.
   */
  async findIfABackupFileExists({
    validateFile = true,
    multipleFiles = false,
    speedUpHeuristic = false,
  } = {}) {
    // If we already have a backup and aren't scanning for multiple files, skip searching
    if (lazy.lastBackupFileName && !multipleFiles) {
      return {
        multipleBackupsFound: false,
        count: 1,
      };
    }

    try {
      // During the first startup, the browser's backup location is often left
      // unconfigured; therefore, it defaults to predefined locations to look
      // for existing backup files.
      let backupPaths = new Set();

      if (this.#_state.backupDirPath) {
        backupPaths.add(this.#_state.backupDirPath);
      }

      for (let dirPath of [
        BackupService.docsDirFolderPath?.path,
        BackupService.oneDriveFolderPath?.path,
      ]) {
        if (dirPath) {
          backupPaths.add(
            PathUtils.join(dirPath, BackupService.BACKUP_DIR_NAME)
          );
        }
      }

      let files = [];
      let anyPathSucceeded = false;

      for (let backupPath of backupPaths) {
        try {
          files.push(
            ...(await IOUtils.getChildren(backupPath, { ignoreAbsent: true }))
          );
          anyPathSucceeded = true;
        } catch (e) {
          lazy.logConsole.error(
            "Could not read backup directory: ",
            backupPath,
            e
          );
        }
      }

      if (!anyPathSucceeded && backupPaths.size) {
        // Since we don't have access to any directory, let's not mistakenly
        // let the user see a state where they can restore from.
        this.#_state.backupFileToRestore = null;
        this.#_state.backupFileInfo = null;
        this.stateUpdate();
        return { multipleBackupsFound: false, count: 0 };
      }

      // filtering is an O(N) operation, we can return early if there's too many files
      // in this folder to filter to avoid a performance bottleneck
      if (speedUpHeuristic && files && files.length > 1000) {
        return {
          multipleBackupsFound: false,
          count: 0,
        };
      }

      // The backup is always a html file and starts with "FirefoxBackup_"
      // disregard any other files in the folder
      let maybeBackupFiles = files.filter(f => {
        let name = PathUtils.filename(f);

        // Note: The Firefox backup filename is localized (see BackupService.BACKUP_FILE_NAME).
        // For now, we use a hardcoded regex string directly for performance reasons.
        return /^FirefoxBackup_.*\.html$/.test(name);
      });

      // if we aren't validating files, and there's more than 1 html file, we decide
      // that there's no valid backup file found
      if (!multipleFiles && maybeBackupFiles.length > 1 && !validateFile) {
        return { multipleBackupsFound: true, count: maybeBackupFiles.length };
      }

      // Sort the files by the timestamp at the end of the filename,
      // so the newest valid file is selected as the file to restore
      if (multipleFiles && maybeBackupFiles.length > 1 && validateFile) {
        maybeBackupFiles.sort((a, b) => {
          let nameA = PathUtils.filename(a);
          let nameB = PathUtils.filename(b);
          const match = /_(\d{8}-\d{6}\.\d{3})\.html$/;
          let timestampA = nameA.match(match)?.[1];
          let timestampB = nameB.match(match)?.[1];

          // If either file doesn't match the expected pattern, maintain the original order
          if (!timestampA || !timestampB) {
            return 0;
          }

          return timestampB.localeCompare(timestampA);
        });
      }

      for (const file of maybeBackupFiles) {
        if (validateFile) {
          try {
            await this.loadBackupFileInfo(file);
          } catch (e) {
            lazy.logConsole.log(
              "Not a valid backup file in the default folder",
              file,
              e
            );

            // If this was previously selected but is no longer valid, unbind it
            if (this.#_state.backupFileToRestore === file) {
              this.#_state.backupFileToRestore = null;
              this.#_state.backupFileInfo = null;
              this.stateUpdate();
            }

            // let's move on to finding another file
            continue;
          }
        }

        this.#_state.backupFileToRestore = file;
        this.stateUpdate();

        // In the case that multiple files were found,
        // but we also validated files to set the newest backup file as the file to restore,
        // we still want to return that multiple backups were found.
        if (multipleFiles && maybeBackupFiles.length > 1 && validateFile) {
          return {
            multipleBackupsFound: true,
            count: maybeBackupFiles.length,
          };
        }

        // TODO: support multiple valid backups for different profiles.
        // Currently, we break out of the loop and select the first profile that works.
        // We want to eventually support showing multiple valid profiles to the user.
        return { multipleBackupsFound: false, count: maybeBackupFiles.length };
      }
    } catch (e) {
      lazy.logConsole.error(
        "There was an error while looking for backups: ",
        e
      );
    }

    return { multipleBackupsFound: false, count: 0 };
  }

  /**
   * Searches for backup files in predefined "well-known" locations.
   *
   * This function wraps findIfABackupFileExists to present the result
   * in an object for processing in the frontend.
   *
   * Assumptions:
   *
   * - Intended to be called before ``about:welcome`` opens.
   * - Clears any existing ``lastBackupFileName`` and ``backupFileToRestore``
   *   in the internal state prior to searching.
   *
   * @param {object} [options] - Configuration options.
   * @param {boolean} [options.validateFile=true] - Whether to validate each backup file
   *   before selecting it.
   * @param {boolean} [options.multipleFiles=true] - Whether to allow selecting a file
   *   when multiple files are found
   * @param {string} [options.source] - If provided, records a
   *   backup_detection_complete telemetry event with this value as the source
   *   (e.g. "onboarding", "preferences"). If omitted, no event is recorded.
   *
   * @returns {Promise<object>} A result object with the following properties:
   * - {boolean} found — Whether a backup file was found.
   * - {string|null} backupFileToRestore — Path or identifier of the backup file (if found).
   * - {boolean} multipleBackupsFound — Currently always `false`, reserved for future use.
   */
  async findBackupsInWellKnownLocations({
    validateFile = true,
    multipleFiles = true,
    source = null,
  } = {}) {
    this.#_state.backupFileToRestore = null;

    let { multipleBackupsFound, count } = await this.findIfABackupFileExists({
      validateFile,
      multipleFiles,
    });

    let found = !!this.#_state.backupFileToRestore;

    if (source) {
      let backupDate = found ? this.#_state.backupFileInfo?.date : null;
      let extra = {
        count,
        source,
        backup_timestamp: backupDate ? new Date(backupDate).getTime() : 0,
        location: found
          ? this.#_state.backupFileCoarseLocation || "other"
          : "none",
        restore_id: found ? this.#_state.restoreID || "" : "",
      };
      Glean.browserBackup.backupDetectionComplete.record(extra);
    }

    if (found) {
      return {
        found: true,
        backupFileToRestore: this.#_state.backupFileToRestore,
        multipleBackupsFound,
      };
    }
    return { found: false, backupFileToRestore: null, multipleBackupsFound };
  }

  /**
   * Sets the location to write the single-file archive files.
   *
   * @param {string} path
   *   The parent directory path where backups should be stored.
   * @returns {Promise<undefined>}
   */
  async editBackupLocation(path) {
    // If the location changed, delete the last backup there if one exists.
    try {
      await this.deleteLastBackup();
    } catch {
      lazy.logConsole.error(
        "Error deleting last backup while editing the backup location."
      );
      // Fall through so the new backup directory is set.
    }
    await this.setParentDirPath(path);
  }

  /**
   * Will attempt to delete the last created single-file archive if it exists.
   * Once done, this method will also check the parent folder to see if it's
   * empty. If so, then the folder is removed.
   *
   * @returns {Promise<undefined>}
   */
  async deleteLastBackup() {
    if (!lazy.scheduledBackupsPref) {
      lazy.logConsole.debug(
        "Not deleting last backup, as scheduled backups are disabled."
      );
      return undefined;
    }

    return locks.request(
      BackupService.WRITE_BACKUP_LOCK_NAME,
      { signal: this.#backupWriteAbortController.signal },
      async () => {
        if (lazy.lastBackupFileName) {
          if (await this.#infalliblePathExists(lazy.backupDirPref)) {
            let backupFilePath = PathUtils.join(
              lazy.backupDirPref,
              lazy.lastBackupFileName
            );

            lazy.logConsole.log(
              "Attempting to delete last backup file at ",
              backupFilePath
            );
            await IOUtils.remove(backupFilePath, {
              ignoreAbsent: true,
              retryReadonly: true,
            });
          }

          Services.prefs.clearUserPref(LAST_BACKUP_FILE_NAME_PREF_NAME);
        } else {
          lazy.logConsole.log(
            "Not deleting last backup file, since none is known about."
          );
        }

        if (await this.#infalliblePathExists(lazy.backupDirPref)) {
          // Remove the desktop.ini file from the previously backed-up folder
          // to ensure it is completely empty and ready for removal.
          await this.maybeCleanupDesktopIni(lazy.backupDirPref);

          // See if there are any other files lingering around in the destination
          // folder. If not, delete that folder too.
          let children = await IOUtils.getChildren(lazy.backupDirPref);
          if (!children.length) {
            await IOUtils.remove(lazy.backupDirPref, { retryReadonly: true });
          }
        }
      }
    );
  }

  /**
   * Wraps an IOUtils.exists in a try/catch and returns true iff the passed
   * path actually exists on the file system. Returns false if the path doesn't
   * exist or is an invalid path.
   *
   * @param {string} path
   *   The path to check for existence.
   * @returns {Promise<boolean>}
   */
  async #infalliblePathExists(path) {
    if (!path) {
      return false;
    }
    let exists = false;
    try {
      exists = await IOUtils.exists(path);
    } catch (e) {
      lazy.logConsole.warn("Path failed existence check :", path);
      return false;
    }
    return exists;
  }

  /**
   * Adds a profile to the list of profiles with backup enabled. No-op if
   * there is no current selectable profile. Defaults to the current profile's
   * ID if none is provided.
   *
   * @param {string} [profileID]
   */
  static maybeAddToEnabledListPref(
    profileID = lazy.SelectableProfileService.currentProfile?.id
  ) {
    if (!lazy.SelectableProfileService.currentProfile) {
      lazy.logConsole.warn(
        "The enabled pref is only to be used for selectable profiles"
      );
      return;
    }

    let profilesEnabledOn = [...lazy.enabledOnProfilesPref];

    if (!profilesEnabledOn.includes(profileID)) {
      profilesEnabledOn.push(profileID);
    }

    Services.prefs.setStringPref(
      BACKUP_ENABLED_ON_PROFILES_PREF_NAME,
      JSON.stringify(profilesEnabledOn)
    );
  }

  /**
   * Removes a profile from the list of profiles with backup enabled. No-op
   * if there is no current selectable profile. Defaults to the current
   * profile's ID if none is provided.
   *
   * @param {string} [profileID]
   */
  static async maybeRemoveFromEnabledListPref(
    profileID = lazy.SelectableProfileService.currentProfile?.id
  ) {
    if (!lazy.SelectableProfileService.currentProfile) {
      lazy.logConsole.warn(
        "The enabled pref is only to be used for selectable profiles"
      );
      return;
    }

    let profilesEnabledOn = lazy.enabledOnProfilesPref.filter(
      id => id !== profileID
    );
    Services.prefs.setStringPref(
      BACKUP_ENABLED_ON_PROFILES_PREF_NAME,
      JSON.stringify(profilesEnabledOn)
    );

    // Since the remove could be happening during shutdown, let's manually do a flush shared pref to ensure
    // the db has the shared pref value before deletion
    await lazy.SelectableProfileService.flushSharedPrefToDatabase(
      BACKUP_ENABLED_ON_PROFILES_PREF_NAME
    );
  }
}
