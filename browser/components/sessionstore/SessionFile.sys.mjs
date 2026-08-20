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

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  sessionStoreLogger:
    "moz-src:///browser/components/sessionstore/SessionLogger.sys.mjs",
  RunState: "moz-src:///browser/components/sessionstore/RunState.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SessionWriter:
    "moz-src:///browser/components/sessionstore/SessionWriter.sys.mjs",
});

const PREF_UPGRADE_BACKUP = "browser.sessionstore.upgradeBackup.latestBuildID";
const PREF_MAX_UPGRADE_BACKUPS =
  "browser.sessionstore.upgradeBackup.maxUpgradeBackups";

const PREF_MAX_SERIALIZE_BACK = "browser.sessionstore.max_serialize_back";
const PREF_MAX_SERIALIZE_FWD = "browser.sessionstore.max_serialize_forward";

export var SessionFile = {
  /**
   * Read the contents of the session file, asynchronously.
   */
  read() {
    return SessionFileInternal.read();
  },
  /**
   * Write the contents of the session file, asynchronously.
   *
   * @param aData - May get changed on shutdown.
   */
  write(aData) {
    return SessionFileInternal.write(aData);
  },
  /**
   * Wipe the contents of the session file, asynchronously.
   */
  wipe() {
    return SessionFileInternal.wipe();
  },

  /**
   * Return the paths to the files used to store, backup, etc.
   * the state of the file.
   */
  get Paths() {
    return SessionFileInternal.Paths;
  },
};

Object.freeze(SessionFile);

const profileDir = PathUtils.profileDir;

var SessionFileInternal = {
  Paths: Object.freeze({
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
      let latestBackupID = SessionFileInternal.latestUpgradeBackupID;
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
      if (SessionFileInternal.latestUpgradeBackupID) {
        // We have an upgradeBackup
        order.push("upgradeBackup");
      }
      return order;
    },
  }),

  // Number of attempted calls to `write`.
  // Note that we may have _attempts > _successes + _failures,
  // if attempts never complete.
  // Used for error reporting.
  _attempts: 0,

  // Number of successful calls to `write`.
  // Used for error reporting.
  _successes: 0,

  // Number of failed calls to `write`.
  // Used for error reporting.
  _failures: 0,

  // `true` once we have initialized SessionWriter.
  _initialized: false,

  // A string that will be set to the session file name part that was read from
  // disk. It will be available _after_ a session file read() is done.
  _readOrigin: null,

  // `true` if the old, uncompressed, file format was used to read from disk, as
  // a fallback mechanism.
  _usingOldExtension: false,

  // The ID of the latest version of Gecko for which we have an upgrade backup
  // or |undefined| if no upgrade backup was ever written.
  get latestUpgradeBackupID() {
    try {
      return Services.prefs.getCharPref(PREF_UPGRADE_BACKUP);
    } catch (ex) {
      return undefined;
    }
  },

  async _readInternal(useOldExtension) {
    let result;
    let noFilesFound = true;
    this._usingOldExtension = useOldExtension;

    // Attempt to load by order of priority from the various backups
    for (let key of this.Paths.loadOrder) {
      let corrupted = false;
      let exists = true;
      try {
        let path;
        let startMs = Date.now();

        let options = {};
        if (useOldExtension) {
          path = this.Paths[key]
            .replace("jsonlz4", "js")
            .replace("baklz4", "bak");
        } else {
          path = this.Paths[key];
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
        Glean.sessionRestore.corruptFile.false.add();
        Glean.sessionRestore.readFile.accumulateSingleSample(
          Date.now() - startMs
        );
        lazy.sessionStoreLogger.debug(`Successful file read of ${key} file`);
        break;
      } catch (ex) {
        if (DOMException.isInstance(ex) && ex.name == "NotFoundError") {
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
        }
      } finally {
        if (exists) {
          noFilesFound = false;
          Glean.sessionRestore.corruptFile[corrupted ? "true" : "false"].add();
          Glean.sessionRestore.backupCanBeLoadedSessionFile.record({
            can_load: (!corrupted).toString(),
            path_key: key,
            loadfail_reason: "N/A",
          });
        }
      }
    }
    return { result, noFilesFound };
  },

  // Find the correct session file and read it.
  async read() {
    // Load session files with lz4 compression.
    let { result, noFilesFound } = await this._readInternal(false);
    if (!result) {
      // No result? Probably because of migration, let's
      // load uncompressed session files.
      let r = await this._readInternal(true);
      result = r.result;
    }

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
    this._readOrigin = result.origin;

    result.noFilesFound = noFilesFound;

    return result;
  },

  // Initialize SessionWriter and return it as a resolved promise.
  getWriter() {
    if (!this._initialized) {
      if (!this._readOrigin) {
        return Promise.reject(
          "SessionFileInternal.getWriter() called too early! Please read the session file from disk first."
        );
      }

      this._initialized = true;
      lazy.SessionWriter.init(
        this._readOrigin,
        this._usingOldExtension,
        this.Paths,
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
  },

  write(aData) {
    if (lazy.RunState.isClosed) {
      return Promise.reject(new Error("SessionFile is closed"));
    }

    let isFinalWrite = false;
    if (lazy.RunState.isClosing) {
      // If shutdown has started, we will want to stop receiving
      // write instructions.
      isFinalWrite = true;
      lazy.RunState.setClosed();
    }

    let performShutdownCleanup =
      isFinalWrite && !lazy.SessionStore.willAutoRestore;

    this._attempts++;
    let options = { isFinalWrite, performShutdownCleanup };
    let promise = this.getWriter().then(writer => writer.write(aData, options));

    // Wait until the write is done.
    promise = promise.then(
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

        this._successes++;
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
        lazy.sessionStoreLogger.error(
          "Could not write session state file ",
          err,
          err.stack
        );
        this._failures++;
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
          attempts: this._attempts,
          successes: this._successes,
          failures: this._failures,
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
  },

  async wipe() {
    const writer = await this.getWriter();
    await writer.wipe();
    // After a wipe, we need to make sure to re-initialize upon the next read(),
    // because the state variables as sent to the writer have changed.
    this._initialized = false;
  },
};
