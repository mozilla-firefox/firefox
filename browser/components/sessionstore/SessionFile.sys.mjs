/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Implementation of all the disk I/O required by the session store.
 * This is a private API, meant to be used only by the session store.
 * It will change. Do not use it for any other purpose.
 *
 * Note that this module depends on SessionWriter and that it enqueues its I/O
 * requests and never attempts to simultaneously execute two I/O requests on
 * the files used by this module from two distinct threads.
 * Otherwise, we could encounter bugs, especially under Windows,
 * e.g. if a request attempts to write sessionstore.js while
 * another attempts to copy that file.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  sessionStoreLogger:
    "moz-src:///browser/components/sessionstore/SessionLogger.sys.mjs",
  RunState: "moz-src:///browser/components/sessionstore/RunState.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SessionWriteError:
    "moz-src:///browser/components/sessionstore/SessionWriter.sys.mjs",
  SessionWriter:
    "moz-src:///browser/components/sessionstore/SessionWriter.sys.mjs",
});

const DEK_NAME_SESSIONSTORE = "sessionstore";

const PREF_UPGRADE_BACKUP = "browser.sessionstore.upgradeBackup.latestBuildID";
const PREF_MAX_UPGRADE_BACKUPS =
  "browser.sessionstore.upgradeBackup.maxUpgradeBackups";

const PREF_MAX_SERIALIZE_BACK = "browser.sessionstore.max_serialize_back";
const PREF_MAX_SERIALIZE_FWD = "browser.sessionstore.max_serialize_forward";

const profileDir = PathUtils.profileDir;

// The ID of the latest version of Gecko for which we have an upgrade backup
// or |undefined| if no upgrade backup was ever written.
function getLatestUpgradeBackupID() {
  try {
    return Services.prefs.getCharPref(PREF_UPGRADE_BACKUP);
  } catch (ex) {
    return undefined;
  }
}

export class SessionFile {
  /**
   * The paths to the files used to store, backup, etc. the state of the file.
   */
  static Paths = Object.freeze({
    // The path to the latest version of sessionstore written during a clean
    // shutdown. After startup, it is renamed `cleanBackup`.
    clean: PathUtils.join(profileDir, "sessionstore.jsonlz4"),

    // The path at which we store the previous version of `clean`. Updated
    // whenever we successfully load from `clean`.
    cleanBackup: PathUtils.join(
      profileDir,
      "sessionstore-backups",
      "previous.jsonlz4"
    ),

    // The directory containing all sessionstore backups.
    backups: PathUtils.join(profileDir, "sessionstore-backups"),

    // The path to the latest version of the sessionstore written
    // during runtime. Generally, this file contains more
    // privacy-sensitive information than |clean|, and this file is
    // therefore removed during clean shutdown. This file is designed to protect
    // against crashes / sudden shutdown.
    recovery: PathUtils.join(
      profileDir,
      "sessionstore-backups",
      "recovery.jsonlz4"
    ),

    // The path to the previous version of the sessionstore written
    // during runtime (e.g. 15 seconds before recovery). In case of a
    // clean shutdown, this file is removed.  Generally, this file
    // contains more privacy-sensitive information than |clean|, and
    // this file is therefore removed during clean shutdown.  This
    // file is designed to protect against crashes that are nasty
    // enough to corrupt |recovery|.
    recoveryBackup: PathUtils.join(
      profileDir,
      "sessionstore-backups",
      "recovery.baklz4"
    ),

    // The path to a backup created during an upgrade of Firefox.
    // Having this backup protects the user essentially from bugs in
    // Firefox or add-ons, especially for users of Nightly. This file
    // does not contain any information more sensitive than |clean|.
    upgradeBackupPrefix: PathUtils.join(
      profileDir,
      "sessionstore-backups",
      "upgrade.jsonlz4-"
    ),

    // The path to the backup of the version of the session store used
    // during the latest upgrade of Firefox. During load/recovery,
    // this file should be used if both |path|, |backupPath| and
    // |latestStartPath| are absent/incorrect.  May be "" if no
    // upgrade backup has ever been performed. This file does not
    // contain any information more sensitive than |clean|.
    get upgradeBackup() {
      let latestBackupID = getLatestUpgradeBackupID();
      if (!latestBackupID) {
        return "";
      }
      return this.upgradeBackupPrefix + latestBackupID;
    },

    // The path to a backup created during an upgrade of Firefox.
    // Having this backup protects the user essentially from bugs in
    // Firefox, especially for users of Nightly.
    get nextUpgradeBackup() {
      return this.upgradeBackupPrefix + Services.appinfo.platformBuildID;
    },

    /**
     * The order in which to search for a valid sessionstore file.
     */
    get loadOrder() {
      // If `clean` exists and has been written without corruption during
      // the latest shutdown, we need to use it.
      //
      // Otherwise, `recovery` and `recoveryBackup` represent the most
      // recent state of the session store.
      //
      // Finally, if nothing works, fall back to the last known state
      // that can be loaded (`cleanBackup`) or, if available, to the
      // backup performed during the latest upgrade.
      let order = ["clean", "recovery", "recoveryBackup", "cleanBackup"];
      if (getLatestUpgradeBackupID()) {
        // We have an upgradeBackup
        order.push("upgradeBackup");
      }
      return order;
    },
  });

  // Number of attempted calls to `write`.
  // Note that we may have _attempts > _successes + _failures,
  // if attempts never complete.
  // Used for error reporting.
  static #attempts = 0;

  // Number of successful calls to `write`.
  // Used for error reporting.
  static #successes = 0;

  // Number of failed calls to `write`.
  // Used for error reporting.
  static #failures = 0;

  // Number of failed calls to `write` since the last successful one.
  static #consecutiveFailures = 0;

  // "success", or "<step>:<cause>" for the last failed write. Used so that a
  // write failing the same way repeatedly is only reported once.
  static #lastWriteOutcome = null;

  // What the last successful write since the session file was read contained,
  // as { windows, tabs, closedWindows, sizeBytes }, or null if there was none.
  static #lastWriteContents = null;

  // `true` once we have initialized SessionWriter.
  static #initialized = false;

  // A string that will be set to the session file name part that was read from
  // disk. It will be available _after_ a session file read() is done.
  static #readOrigin = null;

  // `true` if the old, uncompressed, file format was used to read from disk, as
  // a fallback mechanism.
  static #usingOldExtension = false;

  /**
   * Try each session backup file in priority order, returning the first
   * one that parses successfully.
   *
   * @param {boolean} useOldExtension - If true, look for uncompressed
   *   legacy files (.js/.bak) instead of the current .jsonlz4/.baklz4.
   * @returns {Promise<{ result: object|undefined, fileStates: Record<string, string> }>}
   */
  static async #readInternal(useOldExtension) {
    let result;
    SessionFile.#usingOldExtension = useOldExtension;

    let loadOrder = SessionFile.Paths.loadOrder;
    /** @type {Record<string, string>} */
    let fileStates = {};
    for (let key of loadOrder) {
      fileStates[key] = "not_examined";
    }
    if (!("upgradeBackup" in fileStates)) {
      // `upgradeBackup` is absent from the load order when no upgrade backup
      // was ever taken, which is distinct from the file having gone missing.
      fileStates.upgradeBackup = "not_configured";
    }

    // Attempt to load by order of priority from the various backups
    for (let key of loadOrder) {
      let corrupted = false;
      let exists = true;
      try {
        let path;
        let startMs = Date.now();

        // Always attempt decryption: encrypted files written while the
        // encryption pref was on must be readable even when it is off, and
        // unencrypted/legacy files pass through untouched.
        let options = {};
        if (
          Services.prefs.getBoolPref(
            "browser.sessionstore.encryption.available",
            false
          )
        ) {
          options.decrypt = DEK_NAME_SESSIONSTORE;
        }
        if (useOldExtension) {
          path = SessionFile.Paths[key]
            .replace("jsonlz4", "js")
            .replace("baklz4", "bak");
        } else {
          path = SessionFile.Paths[key];
          options.decompress = true;
        }
        let source = await IOUtils.readUTF8(path, options);
        let parsed = JSON.parse(source);

        if (parsed._cachedObjs) {
          try {
            let cacheMap = new Map(parsed._cachedObjs);
            for (let win of parsed.windows.concat(
              parsed._closedWindows || []
            )) {
              for (let tab of win.tabs.concat(win._closedTabs || [])) {
                tab.image = cacheMap.get(tab.image) || tab.image;
              }
            }
          } catch (e) {
            // This is temporary code to clean up after the backout of bug
            // 1546847. Just in case there are problems in the format of
            // the parsed data, continue on. Favicons might be broken, but
            // the session will at least be recovered
            lazy.sessionStoreLogger.error(e);
          }
        }

        if (
          !lazy.SessionStore.isFormatVersionCompatible(
            parsed.version || [
              "sessionrestore",
              0,
            ] /* fallback for old versions*/
          )
        ) {
          // Skip sessionstore files that we don't understand.
          lazy.sessionStoreLogger.warn(
            "Cannot extract data from Session Restore file ",
            path,
            ". Wrong format/version: " + JSON.stringify(parsed.version) + "."
          );
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason:
              "Wrong format/version: " + JSON.stringify(parsed.version) + ".",
          });
          corrupted = true;
          continue;
        }
        result = {
          origin: key,
          source,
          parsed,
          useOldExtension,
        };
        Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
          can_load: "true",
          path_key: key,
          loadfail_reason: "N/A",
        });
        Glean.sessionRestore.readFile.accumulateSingleSample(
          Date.now() - startMs
        );
        lazy.sessionStoreLogger.debug(`Successful file read of ${key} file`);
        break;
      } catch (ex) {
        let isDecryptionError =
          DOMException.isInstance(ex) &&
          ex.message?.includes("could not decrypt");

        if (isDecryptionError) {
          lazy.sessionStoreLogger.error(
            `Decryption error when reading session file: ${key}`,
            ex
          );
          corrupted = true;
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason: `Decryption error`,
          });
        } else if (DOMException.isInstance(ex) && ex.name == "NotFoundError") {
          exists = false;
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason: "File doesn't exist.",
          });
          // A file not existing can be normal and expected.
          lazy.sessionStoreLogger.debug(
            `Can't read session file which doesn't exist: ${key}`
          );
        } else if (
          DOMException.isInstance(ex) &&
          ex.name == "NotReadableError"
        ) {
          // The file might incorrectly jsonlz4 encoded
          // We'll count it as "corrupted".
          lazy.sessionStoreLogger.error(
            `NotReadableError when reading session file: ${key}`,
            ex
          );
          corrupted = true;
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason: ` ${ex.name}: Could not read session file`,
          });
        } else if (
          DOMException.isInstance(ex) &&
          ex.name == "NotAllowedError"
        ) {
          // The file might be inaccessible due to wrong permissions
          // or similar failures. We'll just count it as "corrupted".
          lazy.sessionStoreLogger.error(
            `NotAllowedError when reading session file: ${key}`,
            ex
          );
          corrupted = true;
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason: ` ${ex.name}: Could not read session file`,
          });
        } else if (ex instanceof SyntaxError) {
          lazy.sessionStoreLogger.error(
            "Corrupt session file (invalid JSON found) ",
            ex,
            ex.stack
          );
          // File is corrupted, try next file
          corrupted = true;
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason: ` ${ex.name}: Corrupt session file (invalid JSON found)`,
          });
        } else {
          corrupted = true;
          lazy.sessionStoreLogger.error(
            `Unexpected error when reading session file: ${key}`,
            ex
          );
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: "false",
            path_key: key,
            loadfail_reason: ` ${ex.name}: Could not read session file`,
          });
        }
      } finally {
        fileStates[key] = exists ? "present" : "absent";
        if (exists) {
          Glean.sessionRestore.corruptFile[corrupted ? "true" : "false"].add();
        }
      }
    }
    return { result, fileStates };
  }

  /**
   * Combine the file states observed by the two format passes, keeping the
   * strongest observation for each key: a file seen in either format counts as
   * present, and a file looked for in either format counts as examined.
   */
  static #mergeFileStates(first, second) {
    /** @type {Record<string, string>} */
    let merged = {};
    for (let key of Object.keys(first)) {
      if (first[key] == "present" || second[key] == "present") {
        merged[key] = "present";
      } else if (first[key] == "absent" || second[key] == "absent") {
        merged[key] = "absent";
      } else {
        merged[key] = first[key];
      }
    }
    return merged;
  }

  /**
   * Read the contents of the session file, asynchronously.
   *
   * Resolves to the parsed session, along with `fileStates`: what we observed
   * of each candidate file while looking for one to load, keyed by the names
   * used in `Paths.loadOrder`. Values are "present" (the read did not report
   * the file missing), "absent", "not_examined" (an earlier file loaded first,
   * so we never looked) or "not_configured" (no upgrade backup was ever
   * taken). It describes the search, not the contents of the profile
   * directory, and is only complete when nothing could be loaded.
   */
  static async read() {
    // Load session files with lz4 compression.
    let { result, fileStates } = await SessionFile.#readInternal(false);
    if (!result) {
      // No result? Probably because of migration, let's
      // load uncompressed session files.
      let r = await SessionFile.#readInternal(true);
      result = r.result;
      fileStates = SessionFile.#mergeFileStates(fileStates, r.fileStates);
    }

    let noFilesFound = !Object.values(fileStates).some(
      state => state == "present"
    );

    // All files are corrupted if files found but none could deliver a result.
    let allCorrupt = !noFilesFound && !result;
    Glean.sessionRestore.allFilesCorrupt[allCorrupt ? "true" : "false"].add();

    if (!result) {
      // If everything fails, start with an empty session.
      lazy.sessionStoreLogger.warn(
        "No readable session files found to restore, starting with empty session"
      );
      result = {
        origin: "empty",
        source: "",
        parsed: null,
        useOldExtension: false,
      };
    }
    SessionFile.#readOrigin = result.origin;
    SessionFile.#lastWriteContents = null;

    return { ...result, noFilesFound, fileStates };
  }

  // Initialize SessionWriter and return it as a resolved promise.
  static #getWriter() {
    if (!SessionFile.#initialized) {
      if (!SessionFile.#readOrigin) {
        return Promise.reject(
          "SessionFile.#getWriter() called too early! Please read the session file from disk first."
        );
      }

      SessionFile.#initialized = true;
      lazy.SessionWriter.init(
        SessionFile.#readOrigin,
        SessionFile.#usingOldExtension,
        SessionFile.Paths,
        {
          maxUpgradeBackups: Services.prefs.getIntPref(
            PREF_MAX_UPGRADE_BACKUPS,
            3
          ),
          maxSerializeBack: Services.prefs.getIntPref(
            PREF_MAX_SERIALIZE_BACK,
            10
          ),
          maxSerializeForward: Services.prefs.getIntPref(
            PREF_MAX_SERIALIZE_FWD,
            -1
          ),
        }
      );
    }

    return Promise.resolve(lazy.SessionWriter);
  }

  /**
   * Write the contents of the session file, asynchronously.
   *
   * @param aData - May get changed on shutdown.
   */
  static write(aData) {
    if (lazy.RunState.isClosed) {
      return Promise.reject(new Error("SessionFile is closed"));
    }

    let contents = SessionFile.countContents(aData);
    let runState = SessionFile.#describeRunState();

    let isFinalWrite = false;
    if (lazy.RunState.isClosing) {
      // If shutdown has started, we will want to stop receiving
      // write instructions.
      isFinalWrite = true;
      lazy.RunState.setClosed();
    }

    let performShutdownCleanup =
      isFinalWrite && !lazy.SessionStore.willAutoRestore;

    SessionFile.#attempts++;
    Glean.sessionRestore.writeAttempts.add(1);
    let options = { isFinalWrite, performShutdownCleanup };
    let write = SessionFile.#getWriter().then(writer =>
      writer.write(aData, options)
    );

    // Wait until the write is done.
    let promise = write.then(
      msg => {
        // Record how long the write took.
        if (msg.telemetry.writeFileMs) {
          Glean.sessionRestore.writeFile.accumulateSingleSample(
            msg.telemetry.writeFileMs
          );
        }
        if (msg.telemetry.fileSizeBytes) {
          Glean.sessionRestore.fileSizeBytes.accumulate(
            msg.telemetry.fileSizeBytes
          );
        }

        SessionFile.#successes++;
        SessionFile.#recordWriteSuccess();
        SessionFile.#recordWriteContents(
          contents,
          msg.telemetry.fileSizeBytes,
          isFinalWrite,
          runState
        );
        if (msg.result.upgradeBackup) {
          // We have just completed a backup-on-upgrade, store the information
          // in preferences.
          Services.prefs.setCharPref(
            PREF_UPGRADE_BACKUP,
            Services.appinfo.platformBuildID
          );
        }
      },
      err => {
        // Catch and report any errors.
        SessionFile.#failures++;
        SessionFile.#recordWriteFailure(err, isFinalWrite);
        // By not doing anything special here we ensure that |promise| cannot
        // be rejected anymore. The shutdown/cleanup code at the end of the
        // function will thus always be executed.
      }
    );

    // Ensure that we can write sessionstore.js cleanly before the profile
    // becomes unaccessible.
    IOUtils.profileBeforeChange.addBlocker(
      "SessionFile: Finish writing Session Restore data",
      promise,
      {
        fetchState: () => ({
          options,
          attempts: SessionFile.#attempts,
          successes: SessionFile.#successes,
          failures: SessionFile.#failures,
        }),
      }
    );

    // This code will always be executed because |promise| can't fail anymore.
    // We ensured that by having a reject handler that reports the failure but
    // doesn't forward the rejection.
    return promise.then(() => {
      // Remove the blocker, no matter if writing failed or not.
      IOUtils.profileBeforeChange.removeBlocker(promise);

      if (isFinalWrite) {
        Services.obs.notifyObservers(
          null,
          "sessionstore-final-state-write-complete"
        );
      }
    });
  }

  /**
   * Counts the open windows, their tabs and the closed windows in a session
   * state, treating anything that isn't an array as empty so that a malformed
   * state read from disk can be counted too.
   *
   * @param {object} state A session state, as written or as read from disk.
   * @returns {{ windows: number, tabs: number, closedWindows: number }}
   */
  static countContents(state) {
    let windows = Array.isArray(state?.windows) ? state.windows : [];
    let tabs = 0;
    for (let win of windows) {
      tabs += Array.isArray(win?.tabs) ? win.tabs.length : 0;
    }
    return {
      windows: windows.length,
      tabs,
      closedWindows: Array.isArray(state?._closedWindows)
        ? state._closedWindows.length
        : 0,
    };
  }

  static #describeRunState() {
    if (lazy.RunState.isClosing) {
      return "closing";
    }
    if (lazy.RunState.isQuitting) {
      return "quitting";
    }
    return lazy.RunState.isRunning ? "running" : "stopped";
  }

  /**
   * Remembers what a successful write contained and records an
   * empty_session_write event if it was empty and the previous write was not,
   * there was no previous write since the session file was read, or this is
   * the final write.
   *
   * @param {{ windows: number, tabs: number, closedWindows: number }} contents
   * @param {number} sizeBytes
   * @param {boolean} isFinalWrite
   * @param {string} runState
   */
  static #recordWriteContents(contents, sizeBytes, isFinalWrite, runState) {
    let previous = SessionFile.#lastWriteContents;
    SessionFile.#lastWriteContents = { ...contents, sizeBytes };

    if (contents.tabs || (previous && !previous.tabs && !isFinalWrite)) {
      return;
    }

    let extra = {
      windows: contents.windows,
      closed_windows: contents.closedWindows,
      size_bytes: sizeBytes,
      is_final_write: isFinalWrite,
      run_state: runState,
    };
    if (previous) {
      extra.prev_windows = previous.windows;
      extra.prev_tabs = previous.tabs;
      extra.prev_size_bytes = previous.sizeBytes;
    }
    lazy.sessionStoreLogger.debug("Wrote a session with no tabs", extra);
    Glean.sessionRestore.emptySessionWrite.record(extra);
  }

  static #recordWriteSuccess() {
    Glean.sessionRestore.writeOutcome.success.add(1);
    if (SessionFile.#consecutiveFailures) {
      lazy.sessionStoreLogger.warn(
        `Wrote session state file after ${SessionFile.#consecutiveFailures} consecutive failed writes`
      );
    }
    SessionFile.#consecutiveFailures = 0;
    SessionFile.#lastWriteOutcome = "success";
  }

  /**
   * Counts a failed write and, unless it failed the same way as the previous
   * write, logs and records the details. A failed final write is always
   * recorded.
   *
   * @param {*} err The rejection from SessionWriter.write, normally a
   *   SessionWriteError.
   * @param {boolean} isFinalWrite
   */
  static #recordWriteFailure(err, isFinalWrite) {
    SessionFile.#consecutiveFailures++;

    let isWriteError = err instanceof lazy.SessionWriteError;
    /** @type {SessionWriteStep|"unknown"} */
    let step = isWriteError ? err.step : "unknown";
    let error = isWriteError ? err.cause : err;
    let sessionWritten = isWriteError && err.sessionWritten;
    // IOUtils appends the nsresult name to its error messages, which is more
    // specific than the DOMException name.
    let cause =
      /\((NS_ERROR_[A-Z0-9_]+)\)$/.exec(error?.message)?.[1] ??
      (error?.name || typeof error);

    Glean.sessionRestore.writeOutcome[
      sessionWritten ? "session_written_backup_failed" : "failed"
    ].add(1);

    let outcome = `${step}:${cause}`;
    if (outcome == SessionFile.#lastWriteOutcome && !isFinalWrite) {
      return;
    }
    SessionFile.#lastWriteOutcome = outcome;

    lazy.sessionStoreLogger.error(
      `Could not write session state file at step ${step} (${cause}), ` +
        `isFinalWrite: ${isFinalWrite}, consecutive failures: ${SessionFile.#consecutiveFailures}, ` +
        `attempts: ${SessionFile.#attempts}, successes: ${SessionFile.#successes}, failures: ${SessionFile.#failures}`,
      error
    );

    Glean.sessionRestore.writeFailure.record({
      step,
      cause,
      session_written: sessionWritten,
      is_final_write: isFinalWrite,
    });
  }

  /**
   * Wipe the contents of the session file, asynchronously.
   */
  static async wipe() {
    const writer = await SessionFile.#getWriter();
    await writer.wipe();
    // After a wipe, we need to make sure to re-initialize upon the next read(),
    // because the state variables as sent to the writer have changed.
    SessionFile.#initialized = false;
  }
}
