/* -*- Mode: javascript; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { AUSTLMY } from "resource://gre/modules/UpdateTelemetry.sys.mjs";

import {
  Bits,
  BitsRequest,
  BitsUnknownError,
  BitsVerificationError,
} from "resource://gre/modules/Bits.sys.mjs";
import { FileUtils } from "resource://gre/modules/FileUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  UpdateLog: "resource://gre/modules/UpdateLog.sys.mjs",
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "AUS",
  "@mozilla.org/updates/update-service;1",
  "nsIApplicationUpdateService"
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "UM",
  "@mozilla.org/updates/update-manager;1",
  "nsIUpdateManager"
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "CheckSvc",
  "@mozilla.org/updates/update-checker;1",
  "nsIUpdateChecker"
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "UpdateServiceStub",
  "@mozilla.org/updates/update-service-stub;1",
  "nsIApplicationUpdateServiceStub"
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "UpdateMutex",
  "@mozilla.org/updates/update-mutex;1",
  "nsIUpdateMutex"
);

const UPDATESERVICE_CID = Components.ID(
  "{B3C290A6-3943-4B89-8BBE-C01EB7B3B311}"
);

const PREF_APP_UPDATE_ALTUPDATEDIRPATH = "app.update.altUpdateDirPath";
const PREF_APP_UPDATE_BACKGROUNDERRORS = "app.update.backgroundErrors";
const PREF_APP_UPDATE_BACKGROUNDMAXERRORS = "app.update.backgroundMaxErrors";
const PREF_APP_UPDATE_BACKGROUND_ALLOWDOWNLOADSWITHOUTBITS =
  "app.update.background.allowDownloadsWithoutBITS";
const PREF_APP_UPDATE_BITS_ENABLED = "app.update.BITS.enabled";
const PREF_APP_UPDATE_CANCELATIONS = "app.update.cancelations";
const PREF_APP_UPDATE_CANCELATIONS_OSX = "app.update.cancelations.osx";
const PREF_APP_UPDATE_CANCELATIONS_OSX_MAX = "app.update.cancelations.osx.max";
const PREF_APP_UPDATE_CHECK_ONLY_INSTANCE_ENABLED =
  "app.update.checkOnlyInstance.enabled";
const PREF_APP_UPDATE_CHECK_ONLY_INSTANCE_INTERVAL =
  "app.update.checkOnlyInstance.interval";
const PREF_APP_UPDATE_CHECK_ONLY_INSTANCE_TIMEOUT =
  "app.update.checkOnlyInstance.timeout";
const PREF_APP_UPDATE_DOWNLOAD_ATTEMPTS = "app.update.download.attempts";
const PREF_APP_UPDATE_DOWNLOAD_MAXATTEMPTS = "app.update.download.maxAttempts";
const PREF_APP_UPDATE_ELEVATE_NEVER = "app.update.elevate.never";
const PREF_APP_UPDATE_ELEVATE_VERSION = "app.update.elevate.version";
const PREF_APP_UPDATE_ELEVATE_ATTEMPTS = "app.update.elevate.attempts";
const PREF_APP_UPDATE_ELEVATE_MAXATTEMPTS = "app.update.elevate.maxAttempts";
const PREF_APP_UPDATE_LANGPACK_ENABLED = "app.update.langpack.enabled";
const PREF_APP_UPDATE_LANGPACK_TIMEOUT = "app.update.langpack.timeout";
const PREF_APP_UPDATE_NOTIFYDURINGDOWNLOAD = "app.update.notifyDuringDownload";
const PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_ENABLED =
  "app.update.noWindowAutoRestart.enabled";
const PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_DELAY_MS =
  "app.update.noWindowAutoRestart.delayMs";
const PREF_APP_UPDATE_PROMPTWAITTIME = "app.update.promptWaitTime";
const PREF_APP_UPDATE_SERVICE_ENABLED = "app.update.service.enabled";
const PREF_APP_UPDATE_SERVICE_ERRORS = "app.update.service.errors";
const PREF_APP_UPDATE_SERVICE_MAXERRORS = "app.update.service.maxErrors";
const PREF_APP_UPDATE_SOCKET_MAXERRORS = "app.update.socket.maxErrors";
const PREF_APP_UPDATE_SOCKET_RETRYTIMEOUT = "app.update.socket.retryTimeout";
const PREF_APP_UPDATE_STAGING_ENABLED = "app.update.staging.enabled";
const PREF_APP_UPDATE_URL_DETAILS = "app.update.url.details";
const PREF_NETWORK_PROXY_TYPE = "network.proxy.type";

const URI_BRAND_PROPERTIES = "chrome://branding/locale/brand.properties";
const URI_UPDATE_NS = "http://www.mozilla.org/2005/app-update";
const URI_UPDATES_PROPERTIES =
  "chrome://mozapps/locale/update/updates.properties";

const KEY_EXECUTABLE = "XREExeF";
const KEY_UPDROOT = "UpdRootD";
const KEY_OLD_UPDROOT = "OldUpdRootD";

const DIR_UPDATES = "updates";
const DIR_UPDATE_READY = "0";
const DIR_UPDATE_DOWNLOADING = "downloading";

const FILE_ACTIVE_UPDATE_XML = "active-update.xml";
const FILE_BACKUP_UPDATE_LOG = "backup-update.log";
const FILE_BACKUP_UPDATE_ELEVATED_LOG = "backup-update-elevated.log";
const FILE_LAST_UPDATE_LOG = "last-update.log";
const FILE_LAST_UPDATE_ELEVATED_LOG = "last-update-elevated.log";
const FILE_UPDATES_XML = "updates.xml";
const FILE_UPDATE_LOG = "update.log";
const FILE_UPDATE_ELEVATED_LOG = "update-elevated.log";
const FILE_UPDATE_MAR = "update.mar";
const FILE_UPDATE_STATUS = "update.status";
const FILE_UPDATE_TEST = "update.test";
const FILE_UPDATE_VERSION = "update.version";

const STATE_NONE = "null";
const STATE_DOWNLOADING = "downloading";
const STATE_PENDING = "pending";
const STATE_PENDING_SERVICE = "pending-service";
const STATE_PENDING_ELEVATE = "pending-elevate";
const STATE_APPLYING = "applying";
const STATE_APPLIED = "applied";
const STATE_APPLIED_SERVICE = "applied-service";
const STATE_SUCCEEDED = "succeeded";
const STATE_DOWNLOAD_FAILED = "download-failed";
const STATE_FAILED = "failed";

// BITS will keep retrying a download after transient errors, unless this much
// time has passed since there has been download progress.
// Similarly to ...POLL_RATE_MS below, we are much more aggressive when the user
// is watching the download progress.
const BITS_IDLE_NO_PROGRESS_TIMEOUT_SECS = 3600; // 1 hour
const BITS_ACTIVE_NO_PROGRESS_TIMEOUT_SECS = 5;

// These value control how frequently we get updates from the BITS client on
// the progress made downloading. The difference between the two is that the
// active interval is the one used when the user is watching. The idle interval
// is the one used when no one is watching.
const BITS_IDLE_POLL_RATE_MS = 1000;
const BITS_ACTIVE_POLL_RATE_MS = 200;

// The number of update attempts when a write error occurs during an attempt
const MAX_TOTAL_INSTALL_ATTEMPTS = 2;

// The values below used by this code are from common/updatererrors.h
const WRITE_ERROR = 7;
const ELEVATION_CANCELED = 9;
const SERVICE_UPDATER_COULD_NOT_BE_STARTED = 24;
const SERVICE_NOT_ENOUGH_COMMAND_LINE_ARGS = 25;
const SERVICE_UPDATER_SIGN_ERROR = 26;
const SERVICE_UPDATER_COMPARE_ERROR = 27;
const SERVICE_UPDATER_IDENTITY_ERROR = 28;
const SERVICE_STILL_APPLYING_ON_SUCCESS = 29;
const SERVICE_STILL_APPLYING_ON_FAILURE = 30;
const SERVICE_UPDATER_NOT_FIXED_DRIVE = 31;
const SERVICE_COULD_NOT_LOCK_UPDATER = 32;
const SERVICE_INSTALLDIR_ERROR = 33;
const WRITE_ERROR_ACCESS_DENIED = 35;
const WRITE_ERROR_CALLBACK_APP = 37;
const UNEXPECTED_STAGING_ERROR = 43;
const DELETE_ERROR_STAGING_LOCK_FILE = 44;
const SERVICE_COULD_NOT_COPY_UPDATER = 49;
const SERVICE_STILL_APPLYING_TERMINATED = 50;
const SERVICE_STILL_APPLYING_NO_EXIT_CODE = 51;
const SERVICE_COULD_NOT_IMPERSONATE = 58;
const WRITE_ERROR_FILE_COPY = 61;
const WRITE_ERROR_DELETE_FILE = 62;
const WRITE_ERROR_OPEN_PATCH_FILE = 63;
const WRITE_ERROR_PATCH_FILE = 64;
const WRITE_ERROR_APPLY_DIR_PATH = 65;
const WRITE_ERROR_CALLBACK_PATH = 66;
const WRITE_ERROR_FILE_ACCESS_DENIED = 67;
const WRITE_ERROR_DIR_ACCESS_DENIED = 68;
const WRITE_ERROR_DELETE_BACKUP = 69;
const WRITE_ERROR_EXTRACT = 70;

// Error codes 80 through 99 are reserved for UpdateService.sys.mjs and are not
// defined in common/updatererrors.h
const ERR_UPDATER_CRASHED = 89;
const ERR_OLDER_VERSION_OR_SAME_BUILD = 90;
const ERR_UPDATE_STATE_NONE = 91;
const ERR_CHANNEL_CHANGE = 92;
const INVALID_UPDATER_STATE_CODE = 98;
const INVALID_UPDATER_STATUS_CODE = 99;

const SILENT_UPDATE_NEEDED_ELEVATION_ERROR = 105;
const BACKGROUND_TASK_SHARING_VIOLATION = 106;

// Array of write errors to simplify checks for write errors
const WRITE_ERRORS = [
  WRITE_ERROR,
  WRITE_ERROR_ACCESS_DENIED,
  WRITE_ERROR_CALLBACK_APP,
  WRITE_ERROR_FILE_COPY,
  WRITE_ERROR_DELETE_FILE,
  WRITE_ERROR_OPEN_PATCH_FILE,
  WRITE_ERROR_PATCH_FILE,
  WRITE_ERROR_APPLY_DIR_PATH,
  WRITE_ERROR_CALLBACK_PATH,
  WRITE_ERROR_FILE_ACCESS_DENIED,
  WRITE_ERROR_DIR_ACCESS_DENIED,
  WRITE_ERROR_DELETE_BACKUP,
  WRITE_ERROR_EXTRACT,
];

// Array of write errors to simplify checks for service errors
const SERVICE_ERRORS = [
  SERVICE_UPDATER_COULD_NOT_BE_STARTED,
  SERVICE_NOT_ENOUGH_COMMAND_LINE_ARGS,
  SERVICE_UPDATER_SIGN_ERROR,
  SERVICE_UPDATER_COMPARE_ERROR,
  SERVICE_UPDATER_IDENTITY_ERROR,
  SERVICE_STILL_APPLYING_ON_SUCCESS,
  SERVICE_STILL_APPLYING_ON_FAILURE,
  SERVICE_UPDATER_NOT_FIXED_DRIVE,
  SERVICE_COULD_NOT_LOCK_UPDATER,
  SERVICE_INSTALLDIR_ERROR,
  SERVICE_COULD_NOT_COPY_UPDATER,
  SERVICE_STILL_APPLYING_TERMINATED,
  SERVICE_STILL_APPLYING_NO_EXIT_CODE,
  SERVICE_COULD_NOT_IMPERSONATE,
];

// Custom update error codes
const BACKGROUNDCHECK_MULTIPLE_FAILURES = 110;
const NETWORK_ERROR_OFFLINE = 111;

// Error codes should be < 1000. Errors above 1000 represent http status codes
const HTTP_ERROR_OFFSET = 1000;

// The is an HRESULT error that may be returned from the BITS interface
// indicating that access was denied. Often, this error code is returned when
// attempting to access a job created by a different user.
const HRESULT_E_ACCESSDENIED = -2147024891;

const DOWNLOAD_CHUNK_SIZE = 300000; // bytes

// The number of consecutive failures when updating using the service before
// setting the app.update.service.enabled preference to false.
const DEFAULT_SERVICE_MAX_ERRORS = 10;

// The number of consecutive socket errors to allow before falling back to
// downloading a different MAR file or failing if already downloading the full.
const DEFAULT_SOCKET_MAX_ERRORS = 10;

// The number of milliseconds to wait before retrying a connection error.
const DEFAULT_SOCKET_RETRYTIMEOUT = 2000;

// Default maximum number of elevation cancelations per update version before
// giving up.
const DEFAULT_CANCELATIONS_OSX_MAX = 3;

// The interval for the update xml write deferred task.
const XML_SAVER_INTERVAL_MS = 200;

// How long after a patch has downloaded should we wait for language packs to
// update before proceeding anyway.
const LANGPACK_UPDATE_DEFAULT_TIMEOUT = 300000;

// Interval between rechecks for other instances after the initial check finds
// at least one other instance.
const ONLY_INSTANCE_CHECK_DEFAULT_POLL_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

// Wait this long after detecting that another instance is running (having been
// polling that entire time) before giving up and applying the update anyway.
const ONLY_INSTANCE_CHECK_DEFAULT_TIMEOUT_MS = 6 * 60 * 60 * 1000; // 6 hours

// The other instance check timeout can be overridden via a pref, but we limit
// that value to this so that the pref can't effectively disable the feature.
const ONLY_INSTANCE_CHECK_MAX_TIMEOUT_MS = 2 * 24 * 60 * 60 * 1000; // 2 days

// Values to use when polling for staging. See `pollForStagingEnd` for more
// details.
const STAGING_POLLING_MIN_INTERVAL_MS = 15 * 1000; // 15 seconds
const STAGING_POLLING_MAX_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes
const STAGING_POLLING_ATTEMPTS_PER_INTERVAL = 5;
const STAGING_POLLING_MAX_DURATION_MS = 1 * 60 * 60 * 1000; // 1 hour

// This value will be set to true if it appears that BITS is being used by
// another user to download updates. We don't really want two users using BITS
// at once. Computers with many users (ex: a school computer), should not end
// up with dozens of BITS jobs.
var gBITSInUseByAnotherUser = false;
// The update service can be invoked as part of a standalone headless background
// task.  In this context, when the background task kicks off an update
// download, we don't want it to move on to staging. As soon as the download has
// kicked off, the task begins shutting down and, even if the the download
// completes incredibly quickly, we don't want staging to begin while we are
// shutting down. That isn't a well tested scenario and it's possible that it
// could leave us in a bad state.
let gOnlyDownloadUpdatesThisSession = false;
// This will be the backing for `nsIApplicationUpdateService.currentState`
var gUpdateState = Ci.nsIApplicationUpdateService.STATE_IDLE;

/**
 * Simple container and constructor for a Promise and its resolve function.
 */
class SelfContainedPromise {
  constructor() {
    this.promise = new Promise(resolve => {
      this.resolve = resolve;
    });
  }
}

// This will contain a `SelfContainedPromise` that will be used to back
// `nsIApplicationUpdateService.stateTransition`.
var gStateTransitionPromise = new SelfContainedPromise();

ChromeUtils.defineLazyGetter(
  lazy,
  "gUpdateBundle",
  function aus_gUpdateBundle() {
    return Services.strings.createBundle(URI_UPDATES_PROPERTIES);
  }
);

function resetIsBackgroundTaskMode() {
  /**
   * gIsBackgroundTaskMode will be true if Firefox is currently running as a
   * background task. Otherwise it will be false.
   */
  ChromeUtils.defineLazyGetter(
    lazy,
    "gIsBackgroundTaskMode",
    function aus_gCurrentlyRunningAsBackgroundTask() {
      if (!("@mozilla.org/backgroundtasks;1" in Cc)) {
        return false;
      }
      const bts = Cc["@mozilla.org/backgroundtasks;1"].getService(
        Ci.nsIBackgroundTasks
      );
      if (!bts) {
        return false;
      }
      return bts.isBackgroundTaskMode;
    }
  );
}
resetIsBackgroundTaskMode();

// Exported for testing only.
export function testResetIsBackgroundTaskMode() {
  resetIsBackgroundTaskMode();
}

/**
 * Changes `nsIApplicationUpdateService.currentState` and causes
 * `nsIApplicationUpdateService.stateTransition` to resolve.
 */
function transitionState(newState) {
  if (newState == gUpdateState) {
    LOG("transitionState - Not transitioning state because it isn't changing.");
    return;
  }
  LOG(
    `transitionState - "${lazy.AUS.getStateName(gUpdateState)}" -> ` +
      `"${lazy.AUS.getStateName(newState)}".`
  );
  gUpdateState = newState;
  // Assign the new Promise before we resolve the old one just to make sure that
  // anything that runs as a result of `resolve` doesn't end up waiting on the
  // Promise that already resolved.
  let oldStateTransitionPromise = gStateTransitionPromise;
  gStateTransitionPromise = new SelfContainedPromise();
  oldStateTransitionPromise.resolve();
}

/**
 * When a plain JS object is passed through xpconnect the other side sees a
 * wrapped version of the object instead of the real object. Since these two
 * objects are different they act as different keys for Map and WeakMap. However
 * xpconnect gives us a way to get the underlying JS object from the wrapper so
 * this function returns the JS object regardless of whether passed the JS
 * object or its wrapper for use in places where it is unclear which one you
 * have.
 */
function unwrap(obj) {
  return obj.wrappedJSObject ?? obj;
}

/**
 * When an update starts to download (and if the feature is enabled) the add-ons
 * manager starts downloading updated language packs for the new application
 * version. A promise is used to track whether those updates are complete so the
 * front-end is only notified that an application update is ready once the
 * language pack updates have been staged.
 *
 * In order to be able to access that promise from various places in the update
 * service they are cached in this map using the nsIUpdate object as a weak
 * owner. Note that the key should always be the result of calling the above
 * unwrap function on the nsIUpdate to ensure a consistent object is used as the
 * key.
 *
 * When the language packs finish staging the nsIUpdate entriy is removed from
 * this map so if the entry is still there then language pack updates are in
 * progress.
 */
const LangPackUpdates = new WeakMap();

/**
 * When we're polling to see if other running instances of the application have
 * exited, there's no need to ever start polling again in parallel. To prevent
 * doing that, we keep track of the promise that resolves when polling completes
 * and return that if a second simultaneous poll is requested, so that the
 * multiple callers end up waiting for the same promise to resolve.
 */
let gOtherInstancePollPromise;

/**
 * Query the update sync manager to see if another instance of this same
 * installation of this application is currently running, under the context of
 * any operating system user (not just the current one).
 * This function immediately returns the current, instantaneous status of any
 * other instances.
 *
 * @return true if at least one other instance is running, false if not
 */
function isOtherInstanceRunning() {
  const checkEnabled = Services.prefs.getBoolPref(
    PREF_APP_UPDATE_CHECK_ONLY_INSTANCE_ENABLED,
    true
  );
  if (!checkEnabled) {
    LOG("isOtherInstanceRunning - disabled by pref, skipping check");
    return false;
  }

  try {
    let syncManager = Cc[
      "@mozilla.org/updates/update-sync-manager;1"
    ].getService(Ci.nsIUpdateSyncManager);
    return syncManager.isOtherInstanceRunning();
  } catch (ex) {
    LOG(`isOtherInstanceRunning - sync manager failed with exception: ${ex}`);
    return false;
  }
}

/**
 * Query the update sync manager to see if another instance of this same
 * installation of this application is currently running, under the context of
 * any operating system user (not just the one running this instance).
 * This function polls for the status of other instances continually
 * (asynchronously) until either none exist or a timeout expires.
 *
 * @return a Promise that resolves with false if at any point during polling no
 *         other instances can be found, or resolves with true if the timeout
 *         expires when other instances are still running
 */
function waitForOtherInstances() {
  // If we're already in the middle of a poll, reuse it rather than start again.
  if (gOtherInstancePollPromise) {
    return gOtherInstancePollPromise;
  }

  let timeout = Services.prefs.getIntPref(
    PREF_APP_UPDATE_CHECK_ONLY_INSTANCE_TIMEOUT,
    ONLY_INSTANCE_CHECK_DEFAULT_TIMEOUT_MS
  );

  // return immediately if timeout value is invalid.
  if (timeout <= 0) {
    return Promise.resolve(isOtherInstanceRunning());
  }

  // Don't allow the pref to set a super high timeout and break this feature.
  if (timeout > ONLY_INSTANCE_CHECK_MAX_TIMEOUT_MS) {
    timeout = ONLY_INSTANCE_CHECK_MAX_TIMEOUT_MS;
  }

  let interval = Services.prefs.getIntPref(
    PREF_APP_UPDATE_CHECK_ONLY_INSTANCE_INTERVAL,
    ONLY_INSTANCE_CHECK_DEFAULT_POLL_INTERVAL_MS
  );

  if (interval <= 0) {
    interval = ONLY_INSTANCE_CHECK_DEFAULT_POLL_INTERVAL_MS;
  }

  // Don't allow an interval longer than the timeout.
  interval = Math.min(interval, timeout);

  let iterations = 0;
  const maxIterations = Math.ceil(timeout / interval);

  gOtherInstancePollPromise = new Promise(function (resolve) {
    let poll = function () {
      iterations++;
      if (!isOtherInstanceRunning()) {
        LOG("waitForOtherInstances - no other instances found, exiting");
        resolve(false);
        gOtherInstancePollPromise = undefined;
      } else if (iterations >= maxIterations) {
        LOG(
          "waitForOtherInstances - timeout expired while other instances " +
            "are still running"
        );
        resolve(true);
        gOtherInstancePollPromise = undefined;
      } else if (iterations + 1 == maxIterations && timeout % interval != 0) {
        // In case timeout isn't a multiple of interval, set the next timeout
        // for the remainder of the time rather than for the usual interval.
        lazy.setTimeout(poll, timeout % interval);
      } else {
        lazy.setTimeout(poll, interval);
      }
    };

    LOG("waitForOtherInstances - beginning polling");
    poll();
  });

  return gOtherInstancePollPromise;
}

/**
 * Tests to make sure that we can write to a given directory.
 *
 * @param updateTestFile a test file in the directory that needs to be tested.
 * @param createDirectory whether a test directory should be created.
 * @throws if we don't have right access to the directory.
 */
function testWriteAccess(updateTestFile, createDirectory) {
  const NORMAL_FILE_TYPE = Ci.nsIFile.NORMAL_FILE_TYPE;
  const DIRECTORY_TYPE = Ci.nsIFile.DIRECTORY_TYPE;
  if (updateTestFile.exists()) {
    updateTestFile.remove(false);
  }
  updateTestFile.create(
    createDirectory ? DIRECTORY_TYPE : NORMAL_FILE_TYPE,
    createDirectory ? FileUtils.PERMS_DIRECTORY : FileUtils.PERMS_FILE
  );
  updateTestFile.remove(false);
}

/**
 * Tests whether or not the current instance has the update mutex. Tries to
 * acquire it if it is not held currently.
 *
 * @return true if this instance now holds the update mutex or was already
 *         holding
 */
function hasUpdateMutex() {
  return lazy.UpdateMutex.tryLock();
}

/**
 * Determines whether or not all descendants of a directory are writeable.
 * Note: Does not check the root directory itself for writeability.
 *
 * @return true if all descendants are writeable, false otherwise
 */
function areDirectoryEntriesWriteable(aDir) {
  let items = aDir.directoryEntries;
  while (items.hasMoreElements()) {
    let item = items.nextFile;
    if (!item.isWritable()) {
      LOG("areDirectoryEntriesWriteable - unable to write to " + item.path);
      return false;
    }
    if (item.isDirectory() && !areDirectoryEntriesWriteable(item)) {
      return false;
    }
  }
  return true;
}

/**
 * OSX only function to determine if the user requires elevation to be able to
 * write to the application bundle.
 *
 * @return true if elevation is required, false otherwise
 */
function getElevationRequired() {
  if (AppConstants.platform != "macosx") {
    return false;
  }

  try {
    // Recursively check that the application bundle (and its descendants) can
    // be written to.
    LOG(
      "getElevationRequired - recursively testing write access on " +
        getInstallDirRoot().path
    );
    if (
      !getInstallDirRoot().isWritable() ||
      !areDirectoryEntriesWriteable(getInstallDirRoot())
    ) {
      LOG(
        "getElevationRequired - unable to write to application bundle, " +
          "elevation required"
      );
      return true;
    }
  } catch (ex) {
    LOG(
      "getElevationRequired - unable to write to application bundle, " +
        "elevation required. Exception: " +
        ex
    );
    return true;
  }
  LOG(
    "getElevationRequired - able to write to application bundle, elevation " +
      "not required"
  );
  return false;
}

/**
 * A promise that resolves when language packs are downloading or if no language
 * packs are being downloaded.
 */
function promiseLangPacksUpdated(update) {
  let promise = LangPackUpdates.get(unwrap(update));
  if (promise) {
    LOG(
      "promiseLangPacksUpdated - waiting for language pack updates to stage."
    );
    return promise;
  }

  // In case callers rely on a promise just return an already resolved promise.
  return Promise.resolve();
}

/*
 * See nsIUpdateService.idl
 */
function isAppBaseDirWritable() {
  let appDirTestFile = "";

  try {
    appDirTestFile = getAppBaseDir();
    appDirTestFile.append(FILE_UPDATE_TEST);
  } catch (e) {
    LOG(
      "isAppBaseDirWritable - Base directory or test path could not be " +
        `determined: ${e}`
    );
    return false;
  }

  try {
    LOG(
      `isAppBaseDirWritable - testing write access for ${appDirTestFile.path}`
    );

    if (appDirTestFile.exists()) {
      appDirTestFile.remove(false);
    }
    // if we're unable to create the test file this will throw an exception:
    appDirTestFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
    appDirTestFile.remove(false);
    LOG(`isAppBaseDirWritable - Path is writable: ${appDirTestFile.path}`);
    return true;
  } catch (e) {
    LOG(
      `isAppBaseDirWritable - Path '${appDirTestFile.path}' ` +
        `is not writable: ${e}`
    );
  }
  // No write access to the installation directory
  return false;
}

/**
 * Determines whether or not an update can be applied. This is always true on
 * Windows when the service is used. On Mac OS X and Linux, if the user has
 * write access to the update directory this will return true because on OSX we
 * offer users the option to perform an elevated update when necessary and on
 * Linux the update directory is located in the application directory.
 *
 * @return true if an update can be applied, false otherwise
 */
function getCanApplyUpdates() {
  try {
    // Check if it is possible to write to the update directory so clients won't
    // repeatedly try to apply an update without the ability to complete the
    // update process which requires write access to the update directory.
    let updateTestFile = getUpdateFile([FILE_UPDATE_TEST]);
    LOG("getCanApplyUpdates - testing write access " + updateTestFile.path);
    testWriteAccess(updateTestFile, false);
  } catch (e) {
    LOG(
      "getCanApplyUpdates - unable to apply updates without write " +
        "access to the update directory. Exception: " +
        e
    );
    return false;
  }

  if (AppConstants.platform == "macosx" || AppConstants.platform == "win") {
    LOG(
      "getCanApplyUpdates - bypass the write since elevation can be used " +
        "on macOS and Windows"
    );
    return true;
  }

  if (!isAppBaseDirWritable()) {
    LOG(
      "getCanApplyUpdates - unable to apply updates, because the base " +
        "directory is not writable."
    );
    return false;
  }

  LOG("getCanApplyUpdates - able to apply updates");
  return true;
}

/**
 * Whether or not the application can stage an update for the current session.
 * These checks are only performed once per session due to using a lazy getter.
 *
 * @return true if updates can be staged for this session.
 */
ChromeUtils.defineLazyGetter(
  lazy,
  "gCanStageUpdatesSession",
  function aus_gCSUS() {
    if (getElevationRequired()) {
      LOG(
        "gCanStageUpdatesSession - unable to stage updates because elevation " +
          "is required."
      );
      return false;
    }

    try {
      let updateTestFile;
      if (AppConstants.platform == "macosx") {
        updateTestFile = getUpdateFile([FILE_UPDATE_TEST]);
      } else {
        updateTestFile = getInstallDirRoot();
        updateTestFile.append(FILE_UPDATE_TEST);
      }
      LOG(
        "gCanStageUpdatesSession - testing write access " + updateTestFile.path
      );
      testWriteAccess(updateTestFile, true);
      if (AppConstants.platform != "macosx") {
        // On all platforms except Mac, we need to test the parent directory as
        // well, as we need to be able to move files in that directory during the
        // replacing step.
        updateTestFile = getInstallDirRoot().parent;
        updateTestFile.append(FILE_UPDATE_TEST);
        LOG(
          "gCanStageUpdatesSession - testing write access " +
            updateTestFile.path
        );
        updateTestFile.createUnique(
          Ci.nsIFile.DIRECTORY_TYPE,
          FileUtils.PERMS_DIRECTORY
        );
        updateTestFile.remove(false);
      }
    } catch (e) {
      LOG("gCanStageUpdatesSession - unable to stage updates. Exception: " + e);
      // No write privileges
      return false;
    }

    LOG("gCanStageUpdatesSession - able to stage updates");
    return true;
  }
);

/**
 * Whether or not the application can stage an update.
 *
 * @param {boolean} [transient] Whether transient factors such as the update
 *        mutex should be considered.
 * @return true if updates can be staged.
 */
function getCanStageUpdates(transient = true) {
  // If staging updates are disabled, then just bail out!
  if (!Services.prefs.getBoolPref(PREF_APP_UPDATE_STAGING_ENABLED, false)) {
    LOG(
      "getCanStageUpdates - staging updates is disabled by preference " +
        PREF_APP_UPDATE_STAGING_ENABLED
    );
    return false;
  }

  if (AppConstants.platform == "win" && shouldUseService()) {
    // No need to perform directory write checks, the maintenance service will
    // be able to write to all directories.
    LOG("getCanStageUpdates - able to stage updates using the service");
    return true;
  }

  if (transient && !hasUpdateMutex()) {
    LOG(
      "getCanStageUpdates - unable to apply updates because another " +
        "instance of the application is already handling updates for this " +
        "installation."
    );
    return false;
  }

  return lazy.gCanStageUpdatesSession;
}

/*
 * Whether or not the application can use BITS to download updates.
 *
 * @param {boolean} [transient] Whether transient factors such as the update
 *        mutex should be considered.
 * @return A string with one of these values:
 *           CanUseBits
 *           NoBits_NotWindows
 *           NoBits_FeatureOff
 *           NoBits_Pref
 *           NoBits_Proxy
 *           NoBits_OtherUser
 *         These strings are directly compatible with the categories for the
 *         Glean.update.canUseBitsExternal and Glean.update.canUseBitsNotify glean
 *         metrics. If this function is made to return other values, they should
 *         also be added to the labels lists for those probes in Histograms.json
 *         and metrics.yaml.
 */
function getCanUseBits(transient = true) {
  if (AppConstants.platform != "win") {
    LOG("getCanUseBits - Not using BITS because this is not Windows");
    return "NoBits_NotWindows";
  }
  if (!AppConstants.MOZ_BITS_DOWNLOAD) {
    LOG("getCanUseBits - Not using BITS because the feature is disabled");
    return "NoBits_FeatureOff";
  }

  if (!Services.prefs.getBoolPref(PREF_APP_UPDATE_BITS_ENABLED, true)) {
    LOG("getCanUseBits - Not using BITS. Disabled by pref.");
    return "NoBits_Pref";
  }
  // Firefox support for passing proxies to BITS is still rudimentary.
  // For now, disable BITS support on configurations that are not using the
  // standard system proxy.
  let defaultProxy = Ci.nsIProtocolProxyService.PROXYCONFIG_SYSTEM;
  if (
    Services.prefs.getIntPref(PREF_NETWORK_PROXY_TYPE, defaultProxy) !=
      defaultProxy &&
    !Cu.isInAutomation
  ) {
    LOG("getCanUseBits - Not using BITS because of proxy usage");
    return "NoBits_Proxy";
  }
  if (transient && gBITSInUseByAnotherUser) {
    LOG("getCanUseBits - Not using BITS. Already in use by another user");
    return "NoBits_OtherUser";
  }
  LOG("getCanUseBits - BITS can be used to download updates");
  return "CanUseBits";
}

/**
 * Logs a string to the error console. If enabled, also logs to the update
 * messages file.
 * @param   string
 *          The string to write to the error console.
 */
function LOG(string) {
  lazy.UpdateLog.logPrefixedString("AUS:SVC", string);
}

/**
 * Gets the specified directory at the specified hierarchy under the
 * update root directory and creates it if it doesn't exist.
 * @param   pathArray
 *          An array of path components to locate beneath the directory
 *          specified by |key|
 * @return  nsIFile object for the location specified.
 */
function getUpdateDirCreate(pathArray) {
  if (Cu.isInAutomation) {
    // This allows tests to use an alternate updates directory so they can test
    // startup behavior.
    const MAGIC_TEST_ROOT_PREFIX = "<test-root>";
    const PREF_TEST_ROOT = "mochitest.testRoot";
    let alternatePath = Services.prefs.getCharPref(
      PREF_APP_UPDATE_ALTUPDATEDIRPATH,
      null
    );
    if (alternatePath && alternatePath.startsWith(MAGIC_TEST_ROOT_PREFIX)) {
      let testRoot = Services.prefs.getCharPref(PREF_TEST_ROOT);
      let relativePath = alternatePath.substring(MAGIC_TEST_ROOT_PREFIX.length);
      if (AppConstants.platform == "win") {
        relativePath = relativePath.replace(/\//g, "\\");
      }
      alternatePath = testRoot + relativePath;
      let updateDir = Cc["@mozilla.org/file/local;1"].createInstance(
        Ci.nsIFile
      );
      updateDir.initWithPath(alternatePath);
      for (let i = 0; i < pathArray.length; ++i) {
        updateDir.append(pathArray[i]);
      }
      return updateDir;
    }
  }

  let dir = FileUtils.getDir(KEY_UPDROOT, pathArray);
  try {
    dir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
  } catch (ex) {
    if (ex.result != Cr.NS_ERROR_FILE_ALREADY_EXISTS) {
      throw ex;
    }
    // Ignore the exception due to a directory that already exists.
  }
  return dir;
}

/**
 * Gets the application base directory.
 *
 * @return  nsIFile object for the application base directory.
 */
function getAppBaseDir() {
  return Services.dirsvc.get(KEY_EXECUTABLE, Ci.nsIFile).parent;
}

/**
 * Gets the root of the installation directory which is the application
 * bundle directory on Mac OS X and the location of the application binary
 * on all other platforms.
 *
 * @return nsIFile object for the directory
 */
function getInstallDirRoot() {
  let dir = getAppBaseDir();
  if (AppConstants.platform == "macosx") {
    // On macOS, the executable is stored under Contents/MacOS.
    dir = dir.parent.parent;
  }
  return dir;
}

/**
 * Gets the file at the specified hierarchy under the update root directory.
 * @param   pathArray
 *          An array of path components to locate beneath the directory
 *          specified by |key|. The last item in this array must be the
 *          leaf name of a file.
 * @return  nsIFile object for the file specified. The file is NOT created
 *          if it does not exist, however all required directories along
 *          the way are.
 */
function getUpdateFile(pathArray) {
  let file = getUpdateDirCreate(pathArray.slice(0, -1));
  file.append(pathArray[pathArray.length - 1]);
  return file;
}

/**
 * This function is designed to let us slightly clean up the mapping between
 * strings and error codes. So that instead of having:
 *   check_error-2147500036=Connection aborted
 *   check_error-2152398850=Connection aborted
 * We can have:
 *   check_error-connection_aborted=Connection aborted
 * And map both of those error codes to it.
 */
function maybeMapErrorCode(code) {
  switch (code) {
    case Cr.NS_BINDING_ABORTED:
    case Cr.NS_ERROR_ABORT:
      return "connection_aborted";
  }
  return code;
}

/**
 * Returns human readable status text from the updates.properties bundle
 * based on an error code
 * @param   code
 *          The error code to look up human readable status text for
 * @param   defaultCode
 *          The default code to look up should human readable status text
 *          not exist for |code|
 * @return  A human readable status text string
 */
function getStatusTextFromCode(code, defaultCode) {
  code = maybeMapErrorCode(code);

  let reason;
  try {
    reason = lazy.gUpdateBundle.GetStringFromName("check_error-" + code);
    LOG(
      "getStatusTextFromCode - transfer error: " + reason + ", code: " + code
    );
  } catch (e) {
    defaultCode = maybeMapErrorCode(defaultCode);

    // Use the default reason
    reason = lazy.gUpdateBundle.GetStringFromName("check_error-" + defaultCode);
    LOG(
      "getStatusTextFromCode - transfer error: " +
        reason +
        ", default code: " +
        defaultCode
    );
  }
  return reason;
}

/**
 * Get the Ready Update directory. This is the directory that an update
 * should reside in after download has completed but before it has been
 * installed and cleaned up.
 * @return The ready updates directory, as a nsIFile object
 */
function getReadyUpdateDir() {
  return getUpdateDirCreate([DIR_UPDATES, DIR_UPDATE_READY]);
}

/**
 * Get the Downloading Update directory. This is the directory that an update
 * should reside in during download. Once download is completed, it will be
 * moved to the Ready Update directory.
 * @return The downloading update directory, as a nsIFile object
 */
function getDownloadingUpdateDir() {
  return getUpdateDirCreate([DIR_UPDATES, DIR_UPDATE_DOWNLOADING]);
}

/**
 * Reads the update state from the update.status file in the specified
 * directory.
 * @param   dir
 *          The dir to look for an update.status file in
 * @return  The status value of the update.
 */
function readStatusFile(dir) {
  let statusFile = dir.clone();
  statusFile.append(FILE_UPDATE_STATUS);
  let status = readStringFromFile(statusFile) || STATE_NONE;
  LOG("readStatusFile - status: " + status + ", path: " + statusFile.path);
  return status;
}

/**
 * Writes the current update operation/state to a file in the patch
 * directory, indicating to the patching system that operations need
 * to be performed.
 * @param   dir
 *          The patch directory where the update.status file should be
 *          written.
 * @param   state
 *          The state value to write.
 */
function writeStatusFile(dir, state) {
  let statusFile = dir.clone();
  statusFile.append(FILE_UPDATE_STATUS);
  writeStringToFile(statusFile, state);
}

/**
 * Writes the update's application version to a file in the patch directory. If
 * the update doesn't provide application version information via the
 * appVersion attribute the string "null" will be written to the file.
 * This value is compared during startup (in nsUpdateDriver.cpp) to determine if
 * the update should be applied. Note that this won't provide protection from
 * downgrade of the application for the nightly user case where the application
 * version doesn't change.
 * @param   dir
 *          The patch directory where the update.version file should be
 *          written.
 * @param   version
 *          The version value to write. Will be the string "null" when the
 *          update doesn't provide the appVersion attribute in the update xml.
 */
function writeVersionFile(dir, version) {
  let versionFile = dir.clone();
  versionFile.append(FILE_UPDATE_VERSION);
  writeStringToFile(versionFile, version);
}

/**
 * Determines if the service should be used to attempt an update
 * or not.
 *
 * @return  true if the service should be used for updates.
 */
function shouldUseService() {
  // This function will return true if the mantenance service should be used if
  // all of the following conditions are met:
  // 1) This build was done with the maintenance service enabled
  // 2) The maintenance service is installed
  // 3) The pref for using the service is enabled
  if (
    !AppConstants.MOZ_MAINTENANCE_SERVICE ||
    !isServiceInstalled() ||
    !Services.prefs.getBoolPref(PREF_APP_UPDATE_SERVICE_ENABLED, false)
  ) {
    LOG("shouldUseService - returning false");
    return false;
  }

  LOG("shouldUseService - returning true");
  return true;
}

/**
 * Determines if the service is is installed.
 *
 * @return  true if the service is installed.
 */
function isServiceInstalled() {
  if (!AppConstants.MOZ_MAINTENANCE_SERVICE || AppConstants.platform != "win") {
    LOG("isServiceInstalled - returning false");
    return false;
  }

  let installed = 0;
  try {
    let wrk = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );
    wrk.open(
      wrk.ROOT_KEY_LOCAL_MACHINE,
      "SOFTWARE\\Mozilla\\MaintenanceService",
      wrk.ACCESS_READ | wrk.WOW64_64
    );
    installed = wrk.readIntValue("Installed");
    wrk.close();
  } catch (e) {}
  installed = installed == 1; // convert to bool
  LOG("isServiceInstalled - returning " + installed);
  return installed;
}

/**
 * Gets the appropriate pending update state. Returns STATE_PENDING_SERVICE,
 * STATE_PENDING_ELEVATE, or STATE_PENDING.
 */
function getBestPendingState() {
  if (shouldUseService()) {
    return STATE_PENDING_SERVICE;
  } else if (getElevationRequired()) {
    return STATE_PENDING_ELEVATE;
  }
  return STATE_PENDING;
}

/**
 * Removes the contents of the ready update directory and rotates the update
 * logs when present. If the update.log exists in the patch directory this will
 * move the last-update.log if it exists to backup-update.log in the parent
 * directory of the patch directory and then move the update.log in the patch
 * directory to last-update.log in the parent directory of the patch directory.
 *
 * @param aRemovePatchFiles (optional, defaults to true)
 *        When true the update's patch directory contents are removed.
 */
function cleanUpReadyUpdateDir(aRemovePatchFiles = true) {
  let updateDir;
  try {
    updateDir = getReadyUpdateDir();
  } catch (e) {
    LOG(
      "cleanUpReadyUpdateDir - unable to get the updates patch directory. " +
        "Exception: " +
        e
    );
    return;
  }

  // Preserve the last update log files for debugging purposes.
  // Make sure to keep the pairs of logs (ex "last-update.log" and
  // "last-update-elevated.log") together. We don't want to skip moving
  // "last-update-elevated.log" just because there isn't an
  // "update-elevated.log" to take its place.
  let updateLogFile = updateDir.clone();
  updateLogFile.append(FILE_UPDATE_LOG);
  let updateElevatedLogFile = updateDir.clone();
  updateElevatedLogFile.append(FILE_UPDATE_ELEVATED_LOG);
  if (updateLogFile.exists() || updateElevatedLogFile.exists()) {
    const overwriteOrRemoveBackupLog = (log, shouldOverwrite, backupName) => {
      if (shouldOverwrite) {
        try {
          log.moveTo(dir, backupName);
        } catch (e) {
          LOG(
            `cleanUpReadyUpdateDir - failed to rename file '${log.path}' to ` +
              `'${backupName}': ${e.result}`
          );
        }
      } else {
        // If we don't have a file to overwrite this one, make sure we remove
        // it anyways to prevent log pairs from getting mismatched.
        let backupLogFile = dir.clone();
        backupLogFile.append(backupName);
        try {
          backupLogFile.remove(false);
        } catch (e) {
          if (e.result != Cr.NS_ERROR_FILE_NOT_FOUND) {
            LOG(
              `cleanUpReadyUpdateDir - failed to remove file ` +
                `'${backupLogFile.path}': ${e.result}`
            );
          }
        }
      }
    };

    let dir = updateDir.parent;
    let logFile = dir.clone();
    logFile.append(FILE_LAST_UPDATE_LOG);
    const logFileExists = logFile.exists();
    let elevatedLogFile = dir.clone();
    elevatedLogFile.append(FILE_LAST_UPDATE_ELEVATED_LOG);
    const elevatedLogFileExists = elevatedLogFile.exists();
    if (logFileExists || elevatedLogFileExists) {
      overwriteOrRemoveBackupLog(
        logFile,
        logFileExists,
        FILE_BACKUP_UPDATE_LOG
      );
      overwriteOrRemoveBackupLog(
        elevatedLogFile,
        elevatedLogFileExists,
        FILE_BACKUP_UPDATE_ELEVATED_LOG
      );
    }

    overwriteOrRemoveBackupLog(updateLogFile, true, FILE_LAST_UPDATE_LOG);
    overwriteOrRemoveBackupLog(
      updateElevatedLogFile,
      true,
      FILE_LAST_UPDATE_ELEVATED_LOG
    );
  }

  if (aRemovePatchFiles) {
    let dirEntries;
    try {
      dirEntries = updateDir.directoryEntries;
    } catch (ex) {
      // If if doesn't exist, our job is already done.
      if (ex.result == Cr.NS_ERROR_FILE_NOT_FOUND) {
        return;
      }
      throw ex;
    }
    while (dirEntries.hasMoreElements()) {
      let file = dirEntries.nextFile;
      // Now, recursively remove this file.  The recursive removal is needed for
      // Mac OSX because this directory will contain a copy of updater.app,
      // which is itself a directory and the MozUpdater directory on platforms
      // other than Windows.
      try {
        file.remove(true);
      } catch (e) {
        LOG("cleanUpReadyUpdateDir - failed to remove file " + file.path);
      }
    }
  }
}

/**
 * Removes the contents of the update download directory.
 *
 */
function cleanUpDownloadingUpdateDir() {
  let updateDir;
  try {
    updateDir = getDownloadingUpdateDir();
  } catch (e) {
    LOG(
      "cleanUpDownloadUpdatesDir - unable to get the updates patch " +
        "directory. Exception: " +
        e
    );
    return;
  }

  let dirEntries;
  try {
    dirEntries = updateDir.directoryEntries;
  } catch (ex) {
    // If if doesn't exist, our job is already done.
    if (ex.result == Cr.NS_ERROR_FILE_NOT_FOUND) {
      return;
    }
    throw ex;
  }
  while (dirEntries.hasMoreElements()) {
    let file = dirEntries.nextFile;
    // Now, recursively remove this file.
    try {
      file.remove(true);
    } catch (e) {
      LOG("cleanUpDownloadUpdatesDir - failed to remove file " + file.path);
    }
  }
}

/**
 * Clean up the updates list and the directory that contains the update that
 * is ready to be installed.
 *
 * Note - This function causes a state transition to either STATE_DOWNLOADING
 *        or STATE_NONE, depending on whether an update download is in progress.
 */
function cleanupReadyUpdate() {
  // Move the update from the Active Update list into the Past Updates list.
  if (lazy.UM.internal.readyUpdate) {
    LOG("cleanupReadyUpdate - Clearing readyUpdate");
    lazy.UM.internal.addUpdateToHistory(lazy.UM.internal.readyUpdate);
    lazy.UM.internal.readyUpdate = null;
  }
  lazy.UM.saveUpdates();

  let readyUpdateDir = getReadyUpdateDir();
  let shouldSetDownloadingStatus =
    lazy.UM.internal.downloadingUpdate ||
    readStatusFile(readyUpdateDir) == STATE_DOWNLOADING;

  // Now trash the ready update directory, since we're done with it
  cleanUpReadyUpdateDir();

  // We need to handle two similar cases here.
  // The first is where we clean up the ready updates directory while we are in
  // the downloading state. In this case, we remove the update.status file that
  // says we are downloading, even though we should remain in that state.
  // The second case is when we clean up a ready update, but there is also a
  // downloading update (in which case the update status file's state will
  // reflect the state of the ready update, not the downloading one). In that
  // case, instead of reverting to STATE_NONE (which is what we do by removing
  // the status file), we should set our state to downloading.
  if (shouldSetDownloadingStatus) {
    LOG("cleanupReadyUpdate - Transitioning back to downloading state.");
    transitionState(Ci.nsIApplicationUpdateService.STATE_DOWNLOADING);
    writeStatusFile(readyUpdateDir, STATE_DOWNLOADING);
  }
}

/**
 * Clean up updates list and the directory that the currently downloading update
 * is downloaded to.
 *
 * Note - This function may cause a state transition. If the current state is
 *        STATE_DOWNLOADING, this will cause it to change to STATE_NONE.
 */
async function cleanupDownloadingUpdate() {
  // Move the update from the Active Update list into the Past Updates list.
  if (lazy.UM.internal.downloadingUpdate) {
    LOG("cleanupDownloadingUpdate - Clearing downloadingUpdate.");
    await lazy.AUS.wrappedJSObject.cancelDownloadingUpdate();
    lazy.UM.internal.addUpdateToHistory(lazy.UM.internal.downloadingUpdate);
    lazy.UM.internal.downloadingUpdate = null;
  }
  lazy.UM.saveUpdates();

  // Now trash the update download directory, since we're done with it
  cleanUpDownloadingUpdateDir();

  // If the update status file says we are downloading, we should remove that
  // too, since we aren't doing that anymore.
  let readyUpdateDir = getReadyUpdateDir();
  let status = readStatusFile(readyUpdateDir);
  if (status == STATE_DOWNLOADING) {
    let statusFile = readyUpdateDir.clone();
    statusFile.append(FILE_UPDATE_STATUS);
    statusFile.remove(false);
  }
}

/**
 * Clean up updates list, the ready update directory, and the downloading update
 * directory.
 *
 * This is more efficient than calling
 *   cleanupReadyUpdate();
 *   cleanupDownloadingUpdate();
 * because those need some special handling of the update status file to make
 * sure that, for example, cleaning up a ready update doesn't make us forget
 * that we are downloading an update. When we cleanup both updates, we don't
 * need to worry about things like that.
 *
 * Note - This function causes a state transition to STATE_NONE.
 */
async function cleanupActiveUpdates() {
  // Move the update from the Active Update list into the Past Updates list.
  if (lazy.UM.internal.readyUpdate) {
    LOG("cleanupActiveUpdates - Clearing readyUpdate");
    lazy.UM.internal.addUpdateToHistory(lazy.UM.internal.readyUpdate);
    lazy.UM.internal.readyUpdate = null;
  }
  if (lazy.UM.internal.downloadingUpdate) {
    LOG("cleanupActiveUpdates - Clearing downloadingUpdate.");
    await lazy.AUS.wrappedJSObject.cancelDownloadingUpdate();
    lazy.UM.internal.addUpdateToHistory(lazy.UM.internal.downloadingUpdate);
    lazy.UM.internal.downloadingUpdate = null;
  }
  lazy.UM.saveUpdates();

  // Now trash both active update directories, since we're done with them
  cleanUpReadyUpdateDir();
  cleanUpDownloadingUpdateDir();
}

/**
 * Writes a string of text to a file.  A newline will be appended to the data
 * written to the file.  This function only works with ASCII text.
 * @param file An nsIFile indicating what file to write to.
 * @param text A string containing the text to write to the file.
 * @return true on success, false on failure.
 */
function writeStringToFile(file, text) {
  try {
    let fos = FileUtils.openSafeFileOutputStream(file);
    text += "\n";
    fos.write(text, text.length);
    FileUtils.closeSafeFileOutputStream(fos);
  } catch (e) {
    LOG(`writeStringToFile - Failed to write to file: "${file}". Error: ${e}"`);
    return false;
  }
  return true;
}

function readStringFromInputStream(inputStream) {
  var sis = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
    Ci.nsIScriptableInputStream
  );
  sis.init(inputStream);
  var text = sis.read(sis.available());
  sis.close();
  if (text && text[text.length - 1] == "\n") {
    text = text.slice(0, -1);
  }
  return text;
}

/**
 * Reads a string of text from a file.  A trailing newline will be removed
 * before the result is returned.  This function only works with ASCII text.
 */
function readStringFromFile(file) {
  if (!file.exists()) {
    LOG("readStringFromFile - file doesn't exist: " + file.path);
    return null;
  }
  var fis = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fis.init(file, FileUtils.MODE_RDONLY, FileUtils.PERMS_FILE, 0);
  return readStringFromInputStream(fis);
}

/**
 * Attempts to recover from an update error. If successful, `true` will be
 * returned and AUS.currentState will be transitioned.
 */
function handleUpdateFailure(update) {
  if (WRITE_ERRORS.includes(update.errorCode)) {
    let nextState = getBestPendingState();

    // Check how many install attempts we have with this patch
    let totalInstallAttempts =
      update.selectedPatch
        .QueryInterface(Ci.nsIWritablePropertyBag)
        .getProperty("numTotalInstallAttempts") ?? 0;
    // Out of retries, unable to handle the update failure here
    if (totalInstallAttempts >= MAX_TOTAL_INSTALL_ATTEMPTS) {
      return false;
    }

    LOG(
      "handleUpdateFailure - Failure is a write error. Setting state to " +
        nextState
    );
    writeStatusFile(getReadyUpdateDir(), (update.state = nextState));
    transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
    return true;
  }

  if (update.errorCode == BACKGROUND_TASK_SHARING_VIOLATION) {
    let newState = getBestPendingState();
    LOG(
      "handleUpdateFailure - witnessed BACKGROUND_TASK_SHARING_VIOLATION, setting state to " +
        newState
    );
    writeStatusFile(getReadyUpdateDir(), (update.state = newState));
    transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
    return true;
  }

  if (update.errorCode == SILENT_UPDATE_NEEDED_ELEVATION_ERROR) {
    // There's no need to count attempts and escalate: it's expected that the
    // background update task will try to update and fail due to required
    // elevation repeatedly if, for example, the maintenance service is not
    // available (or not functioning) and the installation requires privileges
    // to update.

    let bestState = getBestPendingState();
    LOG(
      "handleUpdateFailure - witnessed SILENT_UPDATE_NEEDED_ELEVATION_ERROR, " +
        "returning to " +
        bestState
    );
    writeStatusFile(getReadyUpdateDir(), (update.state = bestState));

    transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
    // Return true to indicate a recoverable error.
    return true;
  }

  if (update.errorCode == ELEVATION_CANCELED) {
    let elevationAttempts = Services.prefs.getIntPref(
      PREF_APP_UPDATE_ELEVATE_ATTEMPTS,
      0
    );
    elevationAttempts++;
    Services.prefs.setIntPref(
      PREF_APP_UPDATE_ELEVATE_ATTEMPTS,
      elevationAttempts
    );
    let maxAttempts = Math.min(
      Services.prefs.getIntPref(PREF_APP_UPDATE_ELEVATE_MAXATTEMPTS, 2),
      10
    );

    if (elevationAttempts > maxAttempts) {
      LOG(
        "handleUpdateFailure - notifying observers of error. " +
          "topic: update-error, status: elevation-attempts-exceeded"
      );
      Services.obs.notifyObservers(
        update,
        "update-error",
        "elevation-attempts-exceeded"
      );
    } else {
      LOG(
        "handleUpdateFailure - notifying observers of error. " +
          "topic: update-error, status: elevation-attempt-failed"
      );
      Services.obs.notifyObservers(
        update,
        "update-error",
        "elevation-attempt-failed"
      );
    }

    let cancelations = Services.prefs.getIntPref(
      PREF_APP_UPDATE_CANCELATIONS,
      0
    );
    cancelations++;
    Services.prefs.setIntPref(PREF_APP_UPDATE_CANCELATIONS, cancelations);
    if (AppConstants.platform == "macosx") {
      let osxCancelations = Services.prefs.getIntPref(
        PREF_APP_UPDATE_CANCELATIONS_OSX,
        0
      );
      osxCancelations++;
      Services.prefs.setIntPref(
        PREF_APP_UPDATE_CANCELATIONS_OSX,
        osxCancelations
      );
      let maxCancels = Services.prefs.getIntPref(
        PREF_APP_UPDATE_CANCELATIONS_OSX_MAX,
        DEFAULT_CANCELATIONS_OSX_MAX
      );
      // Prevent the preference from setting a value greater than 5.
      maxCancels = Math.min(maxCancels, 5);
      if (osxCancelations >= maxCancels) {
        LOG(
          "handleUpdateFailure - Too many OSX cancellations. Cleaning up " +
            "ready update."
        );
        cleanupReadyUpdate();
        return false;
      }
      LOG(
        `handleUpdateFailure - OSX cancellation. Trying again by setting ` +
          `status to "${STATE_PENDING_ELEVATE}".`
      );
      writeStatusFile(
        getReadyUpdateDir(),
        (update.state = STATE_PENDING_ELEVATE)
      );
      update.statusText =
        lazy.gUpdateBundle.GetStringFromName("elevationFailure");
    } else {
      const nextState = getBestPendingState();
      LOG(
        `handleUpdateFailure - Failure because elevation was cancelled. ` +
          `Setting status to ${nextState}.`
      );
      writeStatusFile(getReadyUpdateDir(), (update.state = nextState));
    }
    transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
    return true;
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_CANCELATIONS)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_CANCELATIONS);
  }
  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_CANCELATIONS_OSX)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_CANCELATIONS_OSX);
  }

  if (SERVICE_ERRORS.includes(update.errorCode)) {
    var failCount = Services.prefs.getIntPref(
      PREF_APP_UPDATE_SERVICE_ERRORS,
      0
    );
    var maxFail = Services.prefs.getIntPref(
      PREF_APP_UPDATE_SERVICE_MAXERRORS,
      DEFAULT_SERVICE_MAX_ERRORS
    );
    // Prevent the preference from setting a value greater than 10.
    maxFail = Math.min(maxFail, 10);
    // As a safety, when the service reaches maximum failures, it will
    // disable itself and fallback to using the normal update mechanism
    // without the service.
    if (failCount >= maxFail) {
      Services.prefs.setBoolPref(PREF_APP_UPDATE_SERVICE_ENABLED, false);
      Services.prefs.clearUserPref(PREF_APP_UPDATE_SERVICE_ERRORS);
    } else {
      failCount++;
      Services.prefs.setIntPref(PREF_APP_UPDATE_SERVICE_ERRORS, failCount);
    }

    LOG(
      "handleUpdateFailure - Got a service error. Try to update without the " +
        "service by setting the state to pending."
    );
    writeStatusFile(getReadyUpdateDir(), (update.state = STATE_PENDING));
    transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
    return true;
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_SERVICE_ERRORS)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_SERVICE_ERRORS);
  }

  return false;
}

/**
 * Return the first UpdatePatch with the given type.
 * @param   update
 *          A nsIUpdate object to search through for a patch of the desired
 *          type.
 * @param   patch_type
 *          The type of the patch ("complete" or "partial")
 * @return  A nsIUpdatePatch object matching the type specified
 */
function getPatchOfType(update, patch_type) {
  for (var i = 0; i < update.patchCount; ++i) {
    var patch = update.getPatchAt(i);
    if (patch && patch.type == patch_type) {
      return patch;
    }
  }
  return null;
}

/**
 * Fall back to downloading a complete update in case an update has failed.
 *
 * This will transition `AUS.currentState` to `STATE_DOWNLOADING` if there is
 * another patch to download, or `STATE_IDLE` if there is not.
 */
async function handleFallbackToCompleteUpdate() {
  // If we failed to install an update, we need to fall back to a complete
  // update. If the install directory has been modified, more partial updates
  // will fail for the same reason. Since we only download partial updates
  // while there is already an update downloaded, we don't have to check the
  // downloading update, we can be confident that we are not downloading the
  // right thing at the moment.

  // The downloading update will be newer than the ready update, so use that
  // update, if it exists.
  let update =
    lazy.UM.internal.downloadingUpdate || lazy.UM.internal.readyUpdate;
  if (!update) {
    LOG(
      "handleFallbackToCompleteUpdate - Unable to find an update to fall " +
        "back to."
    );
    return;
  }

  LOG(
    "handleFallbackToCompleteUpdate - Cleaning up active updates in " +
      "preparation of falling back to complete update."
  );
  await cleanupActiveUpdates();

  if (!update.selectedPatch) {
    // If we don't have a partial patch selected but a partial is available,
    // _selectPatch() will download that instead of the complete patch.
    let patch = getPatchOfType(update, "partial");
    if (patch) {
      patch.selected = true;
    }
  }

  update.statusText = lazy.gUpdateBundle.GetStringFromName("patchApplyFailure");
  var oldType = update.selectedPatch ? update.selectedPatch.type : "complete";
  if (update.selectedPatch && oldType == "partial" && update.patchCount == 2) {
    // Partial patch application failed, try downloading the complete
    // update in the background instead.
    LOG(
      "handleFallbackToCompleteUpdate - install of partial patch " +
        "failed, downloading complete patch"
    );
    var result = await lazy.AUS.internal.downloadUpdate(update);
    if (result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS) {
      LOG(
        "handleFallbackToCompleteUpdate - Starting complete patch download " +
          "failed. Cleaning up downloading patch."
      );
      await cleanupDownloadingUpdate();
    }
  } else {
    LOG(
      "handleFallbackToCompleteUpdate - install of complete or " +
        "only one patch offered failed. Notifying observers. topic: " +
        "update-error, status: unknown, " +
        "update.patchCount: " +
        update.patchCount +
        ", " +
        "oldType: " +
        oldType
    );
    transitionState(Ci.nsIApplicationUpdateService.STATE_IDLE);
    Services.obs.notifyObservers(update, "update-error", "unknown");
  }
}

function pingStateAndStatusCodes(aUpdate, aStartup, aStatus) {
  let patchType = AUSTLMY.PATCH_UNKNOWN;
  if (aUpdate && aUpdate.selectedPatch && aUpdate.selectedPatch.type) {
    if (aUpdate.selectedPatch.type == "complete") {
      patchType = AUSTLMY.PATCH_COMPLETE;
    } else if (aUpdate.selectedPatch.type == "partial") {
      patchType = AUSTLMY.PATCH_PARTIAL;
    }
  }

  let suffix = patchType + (aStartup ? AUSTLMY.STARTUP : AUSTLMY.STAGE);
  let stateCode = 0;
  let parts = aStatus.split(":");
  if (parts.length) {
    switch (parts[0]) {
      case STATE_NONE:
        stateCode = 2;
        break;
      case STATE_DOWNLOADING:
        stateCode = 3;
        break;
      case STATE_PENDING:
        stateCode = 4;
        break;
      case STATE_PENDING_SERVICE:
        stateCode = 5;
        break;
      case STATE_APPLYING:
        stateCode = 6;
        break;
      case STATE_APPLIED:
        stateCode = 7;
        break;
      case STATE_APPLIED_SERVICE:
        stateCode = 9;
        break;
      case STATE_SUCCEEDED:
        stateCode = 10;
        break;
      case STATE_DOWNLOAD_FAILED:
        stateCode = 11;
        break;
      case STATE_FAILED:
        stateCode = 12;
        break;
      case STATE_PENDING_ELEVATE:
        stateCode = 13;
        break;
      // Note: Do not use stateCode 14 here. It is defined in
      // UpdateTelemetry.sys.mjs
      default:
        stateCode = 1;
    }

    if (parts.length > 1) {
      let statusErrorCode = INVALID_UPDATER_STATE_CODE;
      if (parts[0] == STATE_FAILED) {
        statusErrorCode = parseInt(parts[1]) || INVALID_UPDATER_STATUS_CODE;
      }
      AUSTLMY.pingStatusErrorCode(suffix, statusErrorCode);
    }
  }
  AUSTLMY.pingStateCode(suffix, stateCode);
}

/**
 * This returns true if the passed update is the same version or older than the
 * version and build ID values passed. Otherwise it returns false.
 */
function updateIsAtLeastAsOldAs(update, version, buildID) {
  if (!update || !update.appVersion || !update.buildID) {
    return false;
  }
  let versionComparison = Services.vc.compare(update.appVersion, version);
  return (
    versionComparison < 0 ||
    (versionComparison == 0 && update.buildID == buildID)
  );
}

/**
 * This returns true if the passed update is the same version or older than
 * currently installed Firefox version.
 */
function updateIsAtLeastAsOldAsCurrentVersion(update) {
  return updateIsAtLeastAsOldAs(
    update,
    Services.appinfo.version,
    Services.appinfo.appBuildID
  );
}

/**
 * This returns true if the passed update is the same version or older than
 * the update that we have already downloaded (UpdateManager.readyUpdate).
 * Returns false if no update has already been downloaded.
 */
function updateIsAtLeastAsOldAsReadyUpdate(update) {
  if (
    !lazy.UM.internal.readyUpdate ||
    !lazy.UM.internal.readyUpdate.appVersion ||
    !lazy.UM.internal.readyUpdate.buildID
  ) {
    return false;
  }
  return updateIsAtLeastAsOldAs(
    update,
    lazy.UM.internal.readyUpdate.appVersion,
    lazy.UM.internal.readyUpdate.buildID
  );
}

/**
 * This function determines whether the error represented by the passed error
 * code could potentially be recovered from or bypassed by updating without
 * using the Maintenance Service (i.e. by showing a UAC prompt).
 * We don't really want to show a UAC prompt, but it's preferable over the
 * manual update doorhanger. So this function effectively distinguishes between
 * which of those we should do if update staging failed. (The updater
 * automatically falls back if the Maintenance Services fails, so this function
 * doesn't handle that case)
 *
 * @param   An integer error code from the update.status file. Should be one of
 *          the codes enumerated in updatererrors.h.
 * @returns true if the code represents a Maintenance Service specific error.
 *          Otherwise, false.
 */
function isServiceSpecificErrorCode(errorCode) {
  return (
    (errorCode >= 24 && errorCode <= 33) || (errorCode >= 49 && errorCode <= 58)
  );
}

/**
 * This function determines whether the error represented by the passed error
 * code is the result of the updater failing to allocate memory. This is
 * relevant when staging because, since Firefox is also running, we may not be
 * able to allocate much memory. Thus, if we fail to stage an update, we may
 * succeed at updating without staging.
 *
 * @param   An integer error code from the update.status file. Should be one of
 *          the codes enumerated in updatererrors.h.
 * @returns true if the code represents a memory allocation error.
 *          Otherwise, false.
 */
function isMemoryAllocationErrorCode(errorCode) {
  return errorCode >= 10 && errorCode <= 14;
}

/**
 * Normally when staging, `nsUpdateProcessor::WaitForProcess` waits for the
 * staging process to complete by watching for its PID to terminate.
 * However, there are less ideal situations. Notably, we might start the browser
 * and find that update staging appears to already be in-progress. If that
 * happens, we really want to pick up the update process from STATE_STAGING,
 * but we don't really have any way of keeping an eye on the staging process
 * other than to just poll the status file.
 *
 * Like `nsUpdateProcessor`, this calls `nsIUpdateManager.refreshUpdateStatus`
 * after polling completes (regardless of result).
 *
 * It is also important to keep in mind that the updater might have crashed
 * during staging, meaning that the status file will never change, no matter how
 * long we keep polling. So we need to set an upper bound on how long we are
 * willing to poll for.
 *
 * There are three situations that we want to avoid.
 * (1) We don't want to set the poll interval too long. A user might be watching
 * the user interface and waiting to restart to install the update. A long poll
 * interval will cause them to have to wait longer than necessary. Especially
 * since the expected total staging time is not that long.
 * (2) We don't want to give up polling too early and give up on an update that
 * will ultimately succeed.
 * (3) We don't want to use a rapid polling interval over a long duration.
 *
 * To avoid these situations, we will start with a short polling interval, but
 * will increase it the longer that we have to wait. Then if we hit the upper
 * bound of polling, we will give up.
 */
function pollForStagingEnd() {
  let pollingIntervalMs = STAGING_POLLING_MIN_INTERVAL_MS;
  // Number of times to poll before increasing the polling interval.
  let pollAttemptsAtIntervalRemaining = STAGING_POLLING_ATTEMPTS_PER_INTERVAL;
  let timeElapsedMs = 0;

  let pollingFn = () => {
    pollAttemptsAtIntervalRemaining -= 1;
    // This isn't a perfectly accurate way of keeping time, but it does nicely
    // sidestep dealing with issues of (non)monotonic time.
    timeElapsedMs += pollingIntervalMs;

    if (timeElapsedMs >= STAGING_POLLING_MAX_DURATION_MS) {
      lazy.UM.internal.refreshUpdateStatus();
      return;
    }

    if (readStatusFile(getReadyUpdateDir()) != STATE_APPLYING) {
      lazy.UM.internal.refreshUpdateStatus();
      return;
    }

    if (pollAttemptsAtIntervalRemaining <= 0) {
      pollingIntervalMs = Math.min(
        pollingIntervalMs * 2,
        STAGING_POLLING_MAX_INTERVAL_MS
      );
      pollAttemptsAtIntervalRemaining = STAGING_POLLING_ATTEMPTS_PER_INTERVAL;
    }

    lazy.setTimeout(pollingFn, pollingIntervalMs);
  };

  lazy.setTimeout(pollingFn, pollingIntervalMs);
}

class UpdatePatch {
  // nsIUpdatePatch attribute names used to prevent nsIWritablePropertyBag from
  // over writing nsIUpdatePatch attributes.
  _attrNames = [
    "errorCode",
    "finalURL",
    "selected",
    "size",
    "state",
    "type",
    "URL",
  ];

  /**
   * @param   patch
   *          A <patch> element to initialize this object with
   * @throws if patch has a size of 0
   * @constructor
   */
  constructor(patch) {
    this._properties = {};
    this.errorCode = 0;
    this.finalURL = null;
    this.state = STATE_NONE;

    for (let i = 0; i < patch.attributes.length; ++i) {
      var attr = patch.attributes.item(i);
      // If an undefined value is saved to the xml file it will be a string when
      // it is read from the xml file.
      if (attr.value == "undefined") {
        continue;
      }
      switch (attr.name) {
        case "xmlns":
          // Don't save the XML namespace.
          break;
        case "selected":
          this.selected = attr.value == "true";
          break;
        case "size":
          if (0 == parseInt(attr.value)) {
            LOG("UpdatePatch:init - 0-sized patch!");
            throw Components.Exception("", Cr.NS_ERROR_ILLEGAL_VALUE);
          }
          this[attr.name] = attr.value;
          break;
        case "errorCode":
          if (attr.value) {
            let val = parseInt(attr.value);
            // This will evaluate to false if the value is 0 but that's ok since
            // this.errorCode is set to the default of 0 above.
            if (val) {
              this.errorCode = val;
            }
          }
          break;
        case "finalURL":
        case "state":
        case "type":
        case "URL":
          this[attr.name] = attr.value;
          break;
        default:
          if (!this._attrNames.includes(attr.name)) {
            // Set nsIPropertyBag properties that were read from the xml file.
            this.setProperty(attr.name, attr.value);
          }
          break;
      }
    }
  }

  /**
   * See nsIUpdateService.idl
   */
  serialize(updates) {
    var patch = updates.createElementNS(URI_UPDATE_NS, "patch");
    patch.setAttribute("size", this.size);
    patch.setAttribute("type", this.type);
    patch.setAttribute("URL", this.URL);
    // Don't write an errorCode if it evaluates to false since 0 is the same as
    // no error code.
    if (this.errorCode) {
      patch.setAttribute("errorCode", this.errorCode);
    }
    // finalURL is not available until after the download has started
    if (this.finalURL) {
      patch.setAttribute("finalURL", this.finalURL);
    }
    // The selected patch is the only patch that should have this attribute.
    if (this.selected) {
      patch.setAttribute("selected", this.selected);
    }
    if (this.state != STATE_NONE) {
      patch.setAttribute("state", this.state);
    }

    for (let [name, value] of Object.entries(this._properties)) {
      if (value.present && !this._attrNames.includes(name)) {
        patch.setAttribute(name, value.data);
      }
    }
    return patch;
  }

  /**
   * See nsIWritablePropertyBag.idl
   */
  setProperty(name, value) {
    if (this._attrNames.includes(name)) {
      throw Components.Exception(
        "Illegal value '" +
          name +
          "' (attribute exists on nsIUpdatePatch) " +
          "when calling method: [nsIWritablePropertyBag::setProperty]",
        Cr.NS_ERROR_ILLEGAL_VALUE
      );
    }
    this._properties[name] = { data: value, present: true };
  }

  /**
   * See nsIWritablePropertyBag.idl
   */
  deleteProperty(name) {
    if (this._attrNames.includes(name)) {
      throw Components.Exception(
        "Illegal value '" +
          name +
          "' (attribute exists on nsIUpdatePatch) " +
          "when calling method: [nsIWritablePropertyBag::deleteProperty]",
        Cr.NS_ERROR_ILLEGAL_VALUE
      );
    }
    if (name in this._properties) {
      this._properties[name].present = false;
    } else {
      throw Components.Exception("", Cr.NS_ERROR_FAILURE);
    }
  }

  /**
   * See nsIPropertyBag.idl
   *
   * Note: this only contains the nsIPropertyBag name / value pairs and not the
   *       nsIUpdatePatch name / value pairs.
   */
  get enumerator() {
    return this.enumerate();
  }

  *enumerate() {
    // An nsISupportsInterfacePointer is used so creating an array using
    // Array.from will retain the QueryInterface for nsIProperty.
    let ip = Cc["@mozilla.org/supports-interface-pointer;1"].createInstance(
      Ci.nsISupportsInterfacePointer
    );
    let qi = ChromeUtils.generateQI([Ci.nsIProperty]);
    for (let [name, value] of Object.entries(this._properties)) {
      if (value.present && !this._attrNames.includes(name)) {
        // The nsIPropertyBag enumerator returns a nsISimpleEnumerator whose
        // elements are nsIProperty objects. Calling QueryInterface for
        // nsIProperty on the object doesn't return to the caller an object that
        // is already queried to nsIProperty but do it just in case it is fixed
        // at some point.
        ip.data = { name, value: value.data, QueryInterface: qi };
        yield ip.data.QueryInterface(Ci.nsIProperty);
      }
    }
  }

  /**
   * See nsIPropertyBag.idl
   *
   * Note: returns null instead of throwing when the property doesn't exist to
   *       simplify code and to silence warnings in debug builds.
   */
  getProperty(name) {
    if (this._attrNames.includes(name)) {
      throw Components.Exception(
        "Illegal value '" +
          name +
          "' (attribute exists on nsIUpdatePatch) " +
          "when calling method: [nsIWritablePropertyBag::getProperty]",
        Cr.NS_ERROR_ILLEGAL_VALUE
      );
    }
    if (name in this._properties && this._properties[name].present) {
      return this._properties[name].data;
    }
    return null;
  }

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIUpdatePatch,
    Ci.nsIPropertyBag,
    Ci.nsIWritablePropertyBag,
  ]);
}

class Update {
  // nsIUpdate attribute names used to prevent nsIWritablePropertyBag from over
  // writing nsIUpdate attributes.
  _attrNames = [
    "appVersion",
    "buildID",
    "channel",
    "detailsURL",
    "displayVersion",
    "elevationFailure",
    "errorCode",
    "installDate",
    "isCompleteUpdate",
    "name",
    "previousAppVersion",
    "promptWaitTime",
    "serviceURL",
    "state",
    "statusText",
    "type",
    "unsupported",
    "platformVersion",
  ];

  /**
   * Implements nsIUpdate
   * @param   update
   *          An <update> element to initialize this object with
   * @throws if the update contains no patches
   * @constructor
   */
  constructor(update) {
    this._patches = [];
    this._properties = {};
    this.isCompleteUpdate = false;
    this.channel = "default";
    this.promptWaitTime = Services.prefs.getIntPref(
      PREF_APP_UPDATE_PROMPTWAITTIME,
      43200
    );
    this.unsupported = false;

    // Null <update>, assume this is a message container and do no
    // further initialization
    if (!update) {
      return;
    }

    for (let i = 0; i < update.childNodes.length; ++i) {
      let patchElement = update.childNodes.item(i);
      if (
        patchElement.nodeType != patchElement.ELEMENT_NODE ||
        patchElement.localName != "patch"
      ) {
        continue;
      }

      let patch;
      try {
        patch = new UpdatePatch(patchElement);
      } catch (e) {
        continue;
      }
      this._patches.push(patch);
    }

    if (!this._patches.length && !update.hasAttribute("unsupported")) {
      throw Components.Exception("", Cr.NS_ERROR_ILLEGAL_VALUE);
    }

    // Set the installDate value with the current time. If the update has an
    // installDate attribute this will be replaced with that value if it doesn't
    // equal 0.
    this.installDate = new Date().getTime();
    this.patchCount = this._patches.length;

    for (let i = 0; i < update.attributes.length; ++i) {
      let attr = update.attributes.item(i);
      if (attr.name == "xmlns" || attr.value == "undefined") {
        // Don't save the XML namespace or undefined values.
        // If an undefined value is saved to the xml file it will be a string when
        // it is read from the xml file.
        continue;
      } else if (attr.name == "detailsURL") {
        this.detailsURL = attr.value;
      } else if (attr.name == "installDate" && attr.value) {
        let val = parseInt(attr.value);
        if (val) {
          this.installDate = val;
        }
      } else if (attr.name == "errorCode" && attr.value) {
        let val = parseInt(attr.value);
        if (val) {
          // Set the value of |_errorCode| instead of |errorCode| since
          // selectedPatch won't be available at this point and normally the
          // nsIUpdatePatch will provide the errorCode.
          this._errorCode = val;
        }
      } else if (attr.name == "isCompleteUpdate") {
        this.isCompleteUpdate = attr.value == "true";
      } else if (attr.name == "promptWaitTime") {
        if (!isNaN(attr.value)) {
          this.promptWaitTime = parseInt(attr.value);
        }
      } else if (attr.name == "unsupported") {
        this.unsupported = attr.value == "true";
      } else {
        switch (attr.name) {
          case "appVersion":
          case "buildID":
          case "channel":
          case "displayVersion":
          case "elevationFailure":
          case "name":
          case "previousAppVersion":
          case "serviceURL":
          case "statusText":
          case "type":
          case "platformVersion":
            this[attr.name] = attr.value;
            break;
          default:
            if (!this._attrNames.includes(attr.name)) {
              // Set nsIPropertyBag properties that were read from the xml file.
              this.setProperty(attr.name, attr.value);
            }
            break;
        }
      }
    }

    if (!this.previousAppVersion) {
      this.previousAppVersion = Services.appinfo.version;
    }

    if (!this.elevationFailure) {
      this.elevationFailure = false;
    }

    if (!this.detailsURL) {
      try {
        // Try using a default details URL supplied by the distribution
        // if the update XML does not supply one.
        this.detailsURL = Services.urlFormatter.formatURLPref(
          PREF_APP_UPDATE_URL_DETAILS
        );
      } catch (e) {
        this.detailsURL = "";
      }
    }

    if (!this.displayVersion) {
      this.displayVersion = this.appVersion;
    }

    if (!this.name) {
      // When the update doesn't provide a name fallback to using
      // "<App Name> <Update App Version>"
      let brandBundle = Services.strings.createBundle(URI_BRAND_PROPERTIES);
      let appName = brandBundle.GetStringFromName("brandShortName");
      this.name = lazy.gUpdateBundle.formatStringFromName("updateName", [
        appName,
        this.displayVersion,
      ]);
    }
  }

  /**
   * See nsIUpdateService.idl
   */
  getPatchAt(index) {
    return this._patches[index];
  }

  /**
   * See nsIUpdateService.idl
   *
   * We use a copy of the state cached on this object in |_state| only when
   * there is no selected patch, i.e. in the case when we could not load
   * active updates from the update manager for some reason but still have
   * the update.status file to work with.
   */
  _state = "";
  get state() {
    if (this.selectedPatch) {
      return this.selectedPatch.state;
    }
    return this._state;
  }
  set state(state) {
    if (this.selectedPatch) {
      this.selectedPatch.state = state;
    }
    this._state = state;
  }

  /**
   * See nsIUpdateService.idl
   *
   * We use a copy of the errorCode cached on this object in |_errorCode| only
   * when there is no selected patch, i.e. in the case when we could not load
   * active updates from the update manager for some reason but still have
   * the update.status file to work with.
   */
  _errorCode = 0;
  get errorCode() {
    if (this.selectedPatch) {
      return this.selectedPatch.errorCode;
    }
    return this._errorCode;
  }
  set errorCode(errorCode) {
    if (this.selectedPatch) {
      this.selectedPatch.errorCode = errorCode;
    }
    this._errorCode = errorCode;
  }

  /**
   * See nsIUpdateService.idl
   */
  get selectedPatch() {
    for (let i = 0; i < this.patchCount; ++i) {
      if (this._patches[i].selected) {
        return this._patches[i];
      }
    }
    return null;
  }

  /**
   * See nsIUpdateService.idl
   */
  serialize(updates) {
    // If appVersion isn't defined just return null. This happens when cleaning
    // up invalid updates (e.g. incorrect channel).
    if (!this.appVersion) {
      return null;
    }
    let update = updates.createElementNS(URI_UPDATE_NS, "update");
    update.setAttribute("appVersion", this.appVersion);
    update.setAttribute("buildID", this.buildID);
    update.setAttribute("channel", this.channel);
    update.setAttribute("detailsURL", this.detailsURL);
    update.setAttribute("displayVersion", this.displayVersion);
    update.setAttribute("platformVersion", this.platformVersion);
    update.setAttribute("installDate", this.installDate);
    update.setAttribute("isCompleteUpdate", this.isCompleteUpdate);
    update.setAttribute("name", this.name);
    update.setAttribute("previousAppVersion", this.previousAppVersion);
    update.setAttribute("promptWaitTime", this.promptWaitTime);
    update.setAttribute("serviceURL", this.serviceURL);
    update.setAttribute("type", this.type);

    if (this.statusText) {
      update.setAttribute("statusText", this.statusText);
    }
    if (this.unsupported) {
      update.setAttribute("unsupported", this.unsupported);
    }
    if (this.elevationFailure) {
      update.setAttribute("elevationFailure", this.elevationFailure);
    }

    for (let [name, value] of Object.entries(this._properties)) {
      if (value.present && !this._attrNames.includes(name)) {
        update.setAttribute(name, value.data);
      }
    }

    for (let i = 0; i < this.patchCount; ++i) {
      update.appendChild(this.getPatchAt(i).serialize(updates));
    }

    updates.documentElement.appendChild(update);
    return update;
  }

  /**
   * See nsIWritablePropertyBag.idl
   */
  setProperty(name, value) {
    if (this._attrNames.includes(name)) {
      throw Components.Exception(
        "Illegal value '" +
          name +
          "' (attribute exists on nsIUpdate) " +
          "when calling method: [nsIWritablePropertyBag::setProperty]",
        Cr.NS_ERROR_ILLEGAL_VALUE
      );
    }
    this._properties[name] = { data: value, present: true };
  }

  /**
   * See nsIWritablePropertyBag.idl
   */
  deleteProperty(name) {
    if (this._attrNames.includes(name)) {
      throw Components.Exception(
        "Illegal value '" +
          name +
          "' (attribute exists on nsIUpdate) " +
          "when calling method: [nsIWritablePropertyBag::deleteProperty]",
        Cr.NS_ERROR_ILLEGAL_VALUE
      );
    }
    if (name in this._properties) {
      this._properties[name].present = false;
    } else {
      throw Components.Exception("", Cr.NS_ERROR_FAILURE);
    }
  }

  /**
   * See nsIPropertyBag.idl
   *
   * Note: this only contains the nsIPropertyBag name value / pairs and not the
   *       nsIUpdate name / value pairs.
   */
  get enumerator() {
    return this.enumerate();
  }

  *enumerate() {
    // An nsISupportsInterfacePointer is used so creating an array using
    // Array.from will retain the QueryInterface for nsIProperty.
    let ip = Cc["@mozilla.org/supports-interface-pointer;1"].createInstance(
      Ci.nsISupportsInterfacePointer
    );
    let qi = ChromeUtils.generateQI([Ci.nsIProperty]);
    for (let [name, value] of Object.entries(this._properties)) {
      if (value.present && !this._attrNames.includes(name)) {
        // The nsIPropertyBag enumerator returns a nsISimpleEnumerator whose
        // elements are nsIProperty objects. Calling QueryInterface for
        // nsIProperty on the object doesn't return to the caller an object that
        // is already queried to nsIProperty but do it just in case it is fixed
        // at some point.
        ip.data = { name, value: value.data, QueryInterface: qi };
        yield ip.data.QueryInterface(Ci.nsIProperty);
      }
    }
  }

  /**
   * See nsIPropertyBag.idl
   * Note: returns null instead of throwing when the property doesn't exist to
   *       simplify code and to silence warnings in debug builds.
   */
  getProperty(name) {
    if (this._attrNames.includes(name)) {
      throw Components.Exception(
        "Illegal value '" +
          name +
          "' (attribute exists on nsIUpdate) " +
          "when calling method: [nsIWritablePropertyBag::getProperty]",
        Cr.NS_ERROR_ILLEGAL_VALUE
      );
    }
    if (name in this._properties && this._properties[name].present) {
      return this._properties[name].data;
    }
    return null;
  }

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIUpdate,
    Ci.nsIPropertyBag,
    Ci.nsIWritablePropertyBag,
  ]);
}

export class UpdateService {
  #initPromise;

  /**
   * The downloader we are using to download updates. There is only ever one of
   * these.
   */
  _downloader = null;

  /**
   * Whether or not the service registered the "online" observer.
   */
  _registeredOnlineObserver = false;

  /**
   * The current number of consecutive socket errors
   */
  _consecutiveSocketErrors = 0;

  /**
   * A timer used to retry socket errors
   */
  _retryTimer = null;

  /**
   * Whether or not a background update check was initiated by the
   * application update timer notification.
   */
  _isNotify = true;

  /**
   * UpdateService
   * A Service for managing the discovery and installation of software updates.
   * @constructor
   */
  constructor() {
    LOG("Creating UpdateService");
    // The observor notification to shut down the service must be before
    // profile-before-change since nsIUpdateManager uses profile-before-change
    // to shutdown and write the update xml files.
    Services.obs.addObserver(this, "quit-application");
    lazy.UpdateLog.addConfigChangeListener(() => {
      this._logStatus();
    });

    this._logStatus();

    this.internal = {
      init: async (force = false) => this.#init(force),
      downloadUpdate: async update => this.#downloadUpdate(update),
      stopDownload: async () => this.#stopDownload(),
      QueryInterface: ChromeUtils.generateQI([
        Ci.nsIApplicationUpdateServiceInternal,
      ]),
    };
  }

  /**
   * See nsIUpdateService.idl
   */
  async init() {
    await lazy.UpdateServiceStub.initUpdate();
  }

  /**
   * See nsIApplicationUpdateServiceInternal.init in nsIUpdateService.idl
   * and `#asyncInit`.
   */
  async #init(force = false) {
    if (force) {
      return this.#asyncInit();
    }
    if (!this.#initPromise) {
      this.#initPromise = this.#asyncInit();
    }

    return this.#initPromise;
  }

  /**
   * Handle Observer Service notifications
   * @param   subject
   *          The subject of the notification
   * @param   topic
   *          The notification name
   * @param   data
   *          Additional data
   */
  async observe(subject, topic, data) {
    switch (topic) {
      case "network:offline-status-changed":
        await this._offlineStatusChanged(data);
        break;
      case "quit-application":
        Services.obs.removeObserver(this, topic);

        if (lazy.UpdateMutex.isLocked()) {
          // If we hold the update mutex, let it go!
          // The OS would clean this up sometime after shutdown,
          // but that would have no guarantee on timing.
          lazy.UpdateMutex.unlock();
        }
        if (this._retryTimer) {
          this._retryTimer.cancel();
        }

        // When downloading an update with nsIIncrementalDownload the download
        // is stopped when the quit-application observer notification is
        // received and networking hasn't started to shutdown. The download will
        // be resumed the next time the application starts. Downloads using
        // Windows BITS are not stopped since they don't require Firefox to be
        // running to perform the download.
        if (this._downloader) {
          if (this._downloader.usingBits) {
            await this._downloader.cleanup();
          } else {
            // stopDownload() calls _downloader.cleanup()
            await this.#stopDownload();
          }
        }
        // Prevent leaking the downloader (bug 454964)
        this._downloader = null;
        // In case any update checks are in progress.
        lazy.CheckSvc.stopAllChecks();
        break;
      case "test-unlock-update-mutex":
        if (Cu.isInAutomation) {
          if (lazy.UpdateMutex.isLocked()) {
            LOG("UpdateService:observe - releasing update mutex for testing");
            lazy.UpdateMutex.unlock();
          }
        }
        break;
    }
  }

  /**
   * This is effectively the initialization function for `UpdateService`.
   *
   * Bug 1900717 - This currently returns immediately if we don't hold the
   *               update mutex when we initialize and it never runs again, even
   *               if we eventually do get the update mutex.
   *
   * The initialization process basically involves doing any of these steps that
   * are currently relevant:
   *
   * Make sure we are in a good state
   *   Reset things and clean up if information in different files conflicts.
   *
   * Show the update elevation dialog
   *   When we need to elevate our privileges to update on macOS, we show a
   *   dialog to ask the user first. We show that now and if they accept it,
   *   they are updated on the next application launch.
   *
   * Post update processing
   *   Move installed updates into the history. Cleanup.
   *
   * Resume in-progress download
   *   For BITS, this really just means connecting to the BITS manager.
   *   For the internal downloader, we actually need to start the download
   *   again.
   *
   * Error fallback
   *   If an in-progress update failed in some way, we try to fall back. This
   *   could include retrying the installation differently next time,
   *   downloading a new MAR, or asking the user to download and run a new
   *   installer.
   */
  /* eslint-disable-next-line complexity */
  async #asyncInit() {
    // This check also ensures we have the update mutex
    if (!this.canCheckForUpdates) {
      LOG(
        "UpdateService:#asyncInit - unable to check for " +
          "updates... returning early"
      );
      return;
    }
    const readyUpdateDir = getReadyUpdateDir();
    let status = readStatusFile(readyUpdateDir);
    let statusParts = status.split(":");
    status = statusParts[0];
    LOG(`UpdateService:#asyncInit - status = "${status}"`);
    if (!this.canUsuallyApplyUpdates) {
      LOG(
        "UpdateService:#asyncInit - unable to apply " +
          "updates... returning early"
      );
      // If the update is present in the update directory somehow,
      // it would prevent us from notifying the user of further updates.
      LOG("UpdateService:#asyncInit - Cleaning up active updates.");
      await cleanupActiveUpdates();
      return;
    }

    let updates = [];
    if (lazy.UM.internal.readyUpdate) {
      updates.push(lazy.UM.internal.readyUpdate);
    }
    if (lazy.UM.internal.downloadingUpdate) {
      updates.push(lazy.UM.internal.downloadingUpdate);
    }

    // Validate the update state. It's too complicated to recover much of
    // anything if the state isn't already correct. And blowing away all the
    // update state is a good way to make sure we always start the update loop
    // in a reasonably consistent state. If we do this, we can return early
    // since the rest of the function deals with further validation and handling
    // of update success and failure.
    const resetUpdateState = async (missingUpdateObject = false) => {
      LOG("UpdateService:#asyncInit - Resetting update state");

      // If we are clearing away some update data and we don't actually have a
      // corresponding update object to write into the update history, just
      // make an empty one.
      if (missingUpdateObject && !updates.length) {
        if (!updates.length) {
          updates.push(new Update(null));
        }
      }

      // We are about to clean up any updates that we have, so if we do have
      // any, mark them as having failed.
      if (updates.length) {
        for (let update of updates) {
          update.state = STATE_FAILED;
          update.errorCode = ERR_UPDATE_STATE_NONE;
          update.statusText =
            lazy.gUpdateBundle.GetStringFromName("statusFailed");
        }
        let newStatus = STATE_FAILED + ": " + ERR_UPDATE_STATE_NONE;
        pingStateAndStatusCodes(updates[0], true, newStatus);
      }

      await cleanupActiveUpdates();
      transitionState(Ci.nsIApplicationUpdateService.STATE_IDLE);
    };

    switch (status) {
      case STATE_DOWNLOADING:
        if (!lazy.UM.internal.downloadingUpdate) {
          LOG("UpdateService:#asyncInit - Missing downloading update object");
          await resetUpdateState(true);
          return;
        } else if (lazy.UM.internal.readyUpdate) {
          // It isn't valid to have a ready update while in the downloading
          // state. We could just clear out the ready update and continue with
          // the downloading update, but if the state is inconsistent, we'd
          // prefer to start from scratch rather than try to rescue it
          LOG("UpdateService:#asyncInit - Unexpected ready update object");
          await resetUpdateState(true);
          return;
        }

        // Note that we don't check for an existing downloading update MAR
        // because there are good states where it does not exist (ex. BITS or
        // the transfer just hasn't started yet).
        break;
      case STATE_PENDING:
      case STATE_PENDING_SERVICE:
      case STATE_PENDING_ELEVATE:
      case STATE_APPLYING:
      case STATE_APPLIED:
      case STATE_APPLIED_SERVICE: {
        let readyMarFile = readyUpdateDir.clone();
        readyMarFile.append(FILE_UPDATE_MAR);

        if (!lazy.UM.internal.readyUpdate) {
          LOG("UpdateService:#asyncInit - Missing ready update object");
          await resetUpdateState(true);
          return;
        } else if (!readyMarFile.exists()) {
          LOG("UpdateService:#asyncInit - Missing mar file");
          await resetUpdateState();
          return;
        }
        break;
      }
      case STATE_SUCCEEDED:
      case STATE_FAILED:
        // There is more handing and validation to be done in this state, so
        // we never want to return early here or lose any of the available state
        // information, even if it is inconsistent.
        break;
      case STATE_DOWNLOAD_FAILED:
        // This is an odd state to start up in since we usually handle this
        // situation right away. We'll just clean this state up since the risk
        // of whatever state is still hanging around interfering with update
        // seems higher than the possible benefit of being able to salvage some
        // partial transfer.
        await resetUpdateState(!lazy.UM.internal.downloadingUpdate);
        return;
      case STATE_NONE: {
        const statusFile = readyUpdateDir.clone();
        statusFile.append(FILE_UPDATE_STATUS);
        // This file existing but not having a state in it is unexpected.
        const statusFileExists = statusFile.exists();
        if (statusFileExists) {
          LOG("UpdateService:#asyncInit - Status file is empty?");
        }
        await resetUpdateState(statusFileExists);
        return;
      }
      default:
        LOG(
          `UpdateService:#asyncInit - Unexpected state! ${status}) - assuming no valid updates`
        );
        await resetUpdateState();
        return;
    }

    let channelChanged = updatesToCheck => {
      for (let update of updatesToCheck) {
        if (update.channel != lazy.UpdateUtils.UpdateChannel) {
          return true;
        }
      }
      return false;
    };
    if (channelChanged(updates)) {
      let channel = lazy.UM.internal.readyUpdate
        ? lazy.UM.internal.readyUpdate.channel
        : lazy.UM.internal.downloadingUpdate.channel;
      LOG(
        "UpdateService:#asyncInit - update channel is " +
          "different than application's channel, removing update. update " +
          "channel: " +
          channel +
          ", expected channel: " +
          lazy.UpdateUtils.UpdateChannel
      );
      // User switched channels, clear out the old active updates and remove
      // partial downloads
      for (let update of updates) {
        update.state = STATE_FAILED;
        update.errorCode = ERR_CHANNEL_CHANGE;
        update.statusText =
          lazy.gUpdateBundle.GetStringFromName("statusFailed");
      }
      let newStatus = STATE_FAILED + ": " + ERR_CHANNEL_CHANGE;
      pingStateAndStatusCodes(updates[0], true, newStatus);
      await cleanupActiveUpdates();
      return;
    }

    // Handle the case when the update is the same or older than the current
    // version and nsUpdateDriver.cpp skipped updating due to the version being
    // older than the current version. This also handles the general case when
    // an update is for an older version or the same version and same build ID.
    if (
      status == STATE_PENDING ||
      status == STATE_PENDING_SERVICE ||
      status == STATE_APPLIED ||
      status == STATE_APPLIED_SERVICE ||
      status == STATE_PENDING_ELEVATE ||
      status == STATE_DOWNLOADING
    ) {
      let tooOldUpdate;
      if (
        updateIsAtLeastAsOldAs(
          lazy.UM.internal.readyUpdate,
          Services.appinfo.version,
          Services.appinfo.appBuildID
        )
      ) {
        tooOldUpdate = lazy.UM.internal.readyUpdate;
      } else if (
        updateIsAtLeastAsOldAs(
          lazy.UM.internal.downloadingUpdate,
          Services.appinfo.version,
          Services.appinfo.appBuildID
        )
      ) {
        tooOldUpdate = lazy.UM.internal.downloadingUpdate;
      }
      if (tooOldUpdate) {
        LOG(
          "UpdateService:#asyncInit - removing update for older " +
            "application version or same application version with same build " +
            "ID. update application version: " +
            tooOldUpdate.appVersion +
            ", " +
            "application version: " +
            Services.appinfo.version +
            ", update " +
            "build ID: " +
            tooOldUpdate.buildID +
            ", application build ID: " +
            Services.appinfo.appBuildID
        );
        tooOldUpdate.state = STATE_FAILED;
        tooOldUpdate.statusText =
          lazy.gUpdateBundle.GetStringFromName("statusFailed");
        tooOldUpdate.errorCode = ERR_OLDER_VERSION_OR_SAME_BUILD;
        // This could be split out to report telemetry for each case.
        let newStatus = STATE_FAILED + ": " + ERR_OLDER_VERSION_OR_SAME_BUILD;
        pingStateAndStatusCodes(tooOldUpdate, true, newStatus);
        // Cleanup both updates regardless of which one is too old. It's
        // exceedingly unlikely that a user could end up in a state where one
        // update is acceptable and the other isn't. And it makes this function
        // considerably more complex to try to deal with that possibility.
        await cleanupActiveUpdates();
        return;
      }
    }

    pingStateAndStatusCodes(
      status == STATE_DOWNLOADING
        ? lazy.UM.internal.downloadingUpdate
        : lazy.UM.internal.readyUpdate,
      true,
      status
    );
    if (lazy.UM.internal.downloadingUpdate || status == STATE_DOWNLOADING) {
      if (status == STATE_SUCCEEDED) {
        // If we successfully installed an update while we were downloading
        // another update, the downloading update is now a partial MAR for
        // a version that is no longer installed. We know that it's a partial
        // MAR without checking because we currently only download partial MARs
        // when an update has already been downloaded.
        LOG(
          "UpdateService:#asyncInit - removing downloading patch " +
            "because we installed a different patch before it finished" +
            "downloading."
        );
        await cleanupDownloadingUpdate();
      } else {
        // Attempt to resume download
        if (lazy.UM.internal.downloadingUpdate) {
          LOG(
            "UpdateService:#asyncInit - resuming patch found in " +
              "downloading state"
          );
          let result = await this.#downloadUpdate(
            lazy.UM.internal.downloadingUpdate
          );
          if (
            result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS &&
            result !=
              Ci.nsIApplicationUpdateService
                .DOWNLOAD_FAILURE_CANNOT_RESUME_IN_BACKGROUND
          ) {
            LOG(
              "UpdateService:#asyncInit - Failed to resume patch. " +
                "Cleaning up downloading update."
            );
            await cleanupDownloadingUpdate();
          }
        } else {
          LOG(
            "UpdateService:#asyncInit - Warning: found " +
              "downloading state, but no downloading patch. Cleaning up " +
              "active updates."
          );
          // Put ourselves back in a good state.
          await cleanupActiveUpdates();
        }
        if (status == STATE_DOWNLOADING) {
          // Done dealing with the downloading update, and there is no ready
          // update, so return early.
          return;
        }
      }
    }

    let update = lazy.UM.internal.readyUpdate;

    if (status == STATE_APPLYING) {
      // This indicates that the background updater service is in either of the
      // following two states:
      // 1. It is in the process of applying an update in the background, and
      //    we just happen to be racing against that.
      // 2. It has failed to apply an update for some reason, and we hit this
      //    case because the updater process has set the update status to
      //    applying, but has never finished.
      // In order to differentiate between these two states, we look at the
      // state field of the update object.  If it's "pending", then we know
      // that this is the first time we're hitting this case, so we switch
      // that state to "applying" and we just wait and hope for the best.
      // If it's "applying", we know that we've already been here once, so
      // we really want to start from a clean state.
      if (
        update &&
        (update.state == STATE_PENDING || update.state == STATE_PENDING_SERVICE)
      ) {
        LOG(
          "UpdateService:#asyncInit - patch found in applying " +
            "state for the first time"
        );
        update.state = STATE_APPLYING;
        lazy.UM.saveUpdates();
        transitionState(Ci.nsIApplicationUpdateService.STATE_STAGING);
        pollForStagingEnd();
      } else {
        // We get here even if we don't have an update object
        LOG(
          "UpdateService:#asyncInit - patch found in applying " +
            "state for the second time. Cleaning up ready update."
        );
        cleanupReadyUpdate();
      }
      return;
    }

    if (!update) {
      if (status != STATE_SUCCEEDED) {
        LOG(
          "UpdateService:#asyncInit - previous patch failed " +
            "and no patch available. Cleaning up ready update."
        );
        cleanupReadyUpdate();
        return;
      }
      LOG(
        "UpdateService:#asyncInit - Update data missing. Creating " +
          "an empty update object."
      );
      update = new Update(null);
    }

    update.state = status;
    LOG(
      `UpdateService:#asyncInit - Setting update's state from ` +
        `the status file (="${update.state}")`
    );
    if (update.state == STATE_FAILED && statusParts[1]) {
      update.errorCode = parseInt(statusParts[1]);
      LOG(
        `UpdateService:#asyncInit - Setting update's errorCode ` +
          `from the status file (="${update.errorCode}")`
      );
    }

    if (status != STATE_SUCCEEDED) {
      // If there are new update logs, rotate them to ensure none are ever
      // overwritten. By passing `false` the patch directory won't be removed.
      cleanUpReadyUpdateDir(false);
    }

    // Track number of installation attempts for this patch
    if (
      update.selectedPatch &&
      [STATE_SUCCEEDED, STATE_FAILED].includes(status)
    ) {
      let totalInstallAttempts =
        update.selectedPatch
          .QueryInterface(Ci.nsIWritablePropertyBag)
          .getProperty("numTotalInstallAttempts") ?? 0;

      update.selectedPatch
        .QueryInterface(Ci.nsIWritablePropertyBag)
        .setProperty("numTotalInstallAttempts", totalInstallAttempts + 1);
    }

    if (status == STATE_SUCCEEDED) {
      if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_CANCELATIONS)) {
        Services.prefs.clearUserPref(PREF_APP_UPDATE_CANCELATIONS);
      }
      update.statusText =
        lazy.gUpdateBundle.GetStringFromName("installSuccess");

      // The only time that update is not a reference to readyUpdate is when
      // readyUpdate is null.
      if (!lazy.UM.internal.readyUpdate) {
        LOG(
          "UpdateService:#asyncInit - Assigning successful update " +
            "readyUpdate before cleaning it up."
        );
        lazy.UM.internal.readyUpdate = update;
      }

      // Done with this update. Clean it up.
      LOG("UpdateService:#asyncInit - Cleaning up successful ready update.");
      cleanupReadyUpdate();

      Services.prefs.setIntPref(PREF_APP_UPDATE_ELEVATE_ATTEMPTS, 0);
    } else if (status == STATE_PENDING_ELEVATE) {
      // In case the active-update.xml file is deleted.
      if (!update) {
        LOG(
          "UpdateService:#asyncInit - status is pending-elevate " +
            "but there isn't a ready update, removing update"
        );
        cleanupReadyUpdate();
      } else {
        transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
        if (Services.startup.wasSilentlyStarted) {
          // This check _should_ be unnecessary since we should not silently
          // restart if state == pending-elevate. But the update elevation
          // dialog is a way that we could potentially show UI on startup, even
          // with no windows open. Which we really do not want to do on a silent
          // restart.
          // So this is defense in depth.
          LOG(
            "UpdateService:#asyncInit - status is " +
              "pending-elevate, but this is a silent startup, so the " +
              "elevation window has been suppressed."
          );
        } else {
          LOG(
            "UpdateService:#asyncInit - status is " +
              "pending-elevate. Showing Update elevation dialog."
          );
          let uri = "chrome://mozapps/content/update/updateElevation.xhtml";
          let features =
            "chrome,centerscreen,resizable=no,titlebar,toolbar=no,dialog=no";
          Services.ww.openWindow(null, uri, "Update:Elevation", features, null);
        }
      }
    } else if (
      status == STATE_PENDING ||
      status == STATE_PENDING_SERVICE ||
      status == STATE_APPLIED ||
      status == STATE_APPLIED_SERVICE
    ) {
      // We started up with an update already pending. Usually we apply updates
      // at startup, but there are some cases where we do not. Most likely, we
      // will just end up installing this update the next time we start up, but
      // we should make sure that the pending update looks valid since it may
      // prevent us from downloading a new one in the meantime.
      LOG("UpdateService:#asyncInit - Verifying existing pending update.");
      // The only things the updater binary really needs to update are
      // `update.status` (which we already read `status` from) and `update.mar`.
      let marFile = readyUpdateDir.clone();
      marFile.append(FILE_UPDATE_MAR);
      if (!marFile.exists()) {
        LOG("UpdateService:#asyncInit - Cleaning up missing pending update.");
        cleanupReadyUpdate();
      }
    } else {
      // If there was an I/O error it is assumed that the patch is not invalid
      // and it is set to pending so an attempt to apply it again will happen
      // when the application is restarted.
      if (update.state == STATE_FAILED && update.errorCode) {
        LOG("UpdateService:#asyncInit - Attempting handleUpdateFailure");
        if (handleUpdateFailure(update)) {
          LOG("UpdateService:#asyncInit - handleUpdateFailure success.");
          return;
        }
      }

      LOG(
        "UpdateService:#asyncInit - Attempting to fall back to a " +
          "complete update."
      );
      // Something went wrong with the patch application process.
      await handleFallbackToCompleteUpdate();
    }
  }

  /**
   * Register an observer when the network comes online, so we can short-circuit
   * the app.update.interval when there isn't connectivity
   */
  _registerOnlineObserver() {
    if (this._registeredOnlineObserver) {
      LOG(
        "UpdateService:_registerOnlineObserver - observer already registered"
      );
      return;
    }

    LOG(
      "UpdateService:_registerOnlineObserver - waiting for the network to " +
        "be online, then forcing another check"
    );

    Services.obs.addObserver(this, "network:offline-status-changed");
    this._registeredOnlineObserver = true;
  }

  /**
   * Called from the network:offline-status-changed observer.
   */
  async _offlineStatusChanged(status) {
    if (status !== "online") {
      return;
    }

    Services.obs.removeObserver(this, "network:offline-status-changed");
    this._registeredOnlineObserver = false;

    LOG(
      "UpdateService:_offlineStatusChanged - network is online, forcing " +
        "another background check"
    );

    // the background checker is contained in notify
    await this._attemptResume();
  }

  /**
   * See nsIUpdateService.idl
   */
  async onCheckComplete(result) {
    if (result.succeeded) {
      await this._selectAndInstallUpdate(result.updates);
      return;
    }

    if (!result.checksAllowed) {
      LOG("UpdateService:onCheckComplete - checks not allowed");
      return;
    }

    // On failure, result.updates is guaranteed to have exactly one update
    // containing error information.
    let update = result.updates[0];

    LOG(
      "UpdateService:onCheckComplete - error during background update. error " +
        "code: " +
        update.errorCode +
        ", status text: " +
        update.statusText
    );

    if (update.errorCode == NETWORK_ERROR_OFFLINE) {
      // Register an online observer to try again
      this._registerOnlineObserver();
      if (this._pingSuffix) {
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_OFFLINE);
      }
      return;
    }

    // Send the error code to telemetry
    AUSTLMY.pingCheckExError(this._pingSuffix, update.errorCode);
    update.errorCode = BACKGROUNDCHECK_MULTIPLE_FAILURES;
    let errCount = Services.prefs.getIntPref(
      PREF_APP_UPDATE_BACKGROUNDERRORS,
      0
    );

    // If we already have an update ready, we don't want to worry the user over
    // update check failures. As far as the user knows, the update status is
    // the status of the ready update. We don't want to confuse them by saying
    // that an update check failed.
    if (lazy.UM.internal.readyUpdate) {
      LOG(
        "UpdateService:onCheckComplete - Ignoring error because another " +
          "update is ready."
      );
      return;
    }

    errCount++;
    Services.prefs.setIntPref(PREF_APP_UPDATE_BACKGROUNDERRORS, errCount);
    // Don't allow the preference to set a value greater than 20 for max errors.
    let maxErrors = Math.min(
      Services.prefs.getIntPref(PREF_APP_UPDATE_BACKGROUNDMAXERRORS, 10),
      20
    );

    if (errCount >= maxErrors) {
      LOG(
        "UpdateService:onCheckComplete - notifying observers of error. " +
          "topic: update-error, status: check-attempts-exceeded"
      );
      Services.obs.notifyObservers(
        update,
        "update-error",
        "check-attempts-exceeded"
      );
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_GENERAL_ERROR_PROMPT);
    } else {
      LOG(
        "UpdateService:onCheckComplete - notifying observers of error. " +
          "topic: update-error, status: check-attempt-failed"
      );
      Services.obs.notifyObservers(
        update,
        "update-error",
        "check-attempt-failed"
      );
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_GENERAL_ERROR_SILENT);
    }
  }

  /**
   * Called when a connection should be resumed
   */
  async _attemptResume() {
    LOG("UpdateService:_attemptResume");
    // If a download is in progress and we aren't already downloading it, then
    // resume it.
    if (this.isDownloading) {
      // There is nothing to resume. We are already downloading.
      LOG("UpdateService:_attemptResume - already downloading.");
      return;
    }
    if (
      this._downloader &&
      this._downloader._patch &&
      this._downloader._patch.state == STATE_DOWNLOADING &&
      this._downloader._update
    ) {
      LOG(
        "UpdateService:_attemptResume - _patch.state: " +
          this._downloader._patch.state
      );
      let result = await this.#downloadUpdate(this._downloader._update);
      LOG("UpdateService:_attemptResume - downloadUpdate result: " + result);
      if (result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS) {
        LOG(
          "UpdateService:_attemptResume - Resuming download failed. Cleaning " +
            "up downloading update."
        );
        await cleanupDownloadingUpdate();
      }
      return;
    }

    // Kick off an update check
    (async () => {
      let check = lazy.CheckSvc.internal.checkForUpdates(
        lazy.CheckSvc.BACKGROUND_CHECK
      );
      await this.onCheckComplete(await check.result);
    })();
  }

  /**
   * Notified when a timer fires
   * @param   _timer
   *          The timer that fired
   */
  async notify(_timer) {
    await this._checkForBackgroundUpdates(true);
  }

  /**
   * See nsIUpdateService.idl
   */
  async checkForBackgroundUpdates() {
    return this._checkForBackgroundUpdates(false);
  }

  // The suffix used for background update check glean metric names.
  get _pingSuffix() {
    if (lazy.UM.internal.readyUpdate) {
      // Once an update has been downloaded, all later updates will be reported
      // to telemetry as subsequent updates. We move the first update into
      // readyUpdate as soon as the download is complete, so any update checks
      // after readyUpdate is no longer null are subsequent update checks.
      return AUSTLMY.SUBSEQUENT;
    }
    return this._isNotify ? AUSTLMY.NOTIFY : AUSTLMY.EXTERNAL;
  }

  /**
   * Checks for updates in the background.
   * @param   isNotify
   *          Whether or not a background update check was initiated by the
   *          application update timer notification.
   */
  async _checkForBackgroundUpdates(isNotify) {
    await this.init();

    if (!this.disabled && AppConstants.NIGHTLY_BUILD) {
      // Scalar ID: update.suppress_prompts
      AUSTLMY.pingSuppressPrompts();
    }
    if (this.disabled || this.manualUpdateOnly) {
      // Return immediately if we are disabled by policy. Otherwise, just the
      // telemetry we try to collect below can potentially trigger a restart
      // prompt if the update directory isn't writable. And we shouldn't be
      // telling the user about update failures if update is disabled.
      // See Bug 1599590.
      // Note that we exit unconditionally here if we are only doing manual
      // update checks, because manual update checking uses a completely
      // different code path (AppUpdater.sys.mjs creates its own nsIUpdateChecker),
      // bypassing this function completely.
      if (!this.disabledForTesting) {
        // This can cause some problems for tests that aren't setup properly for
        // this.
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_DISABLED_BY_POLICY);
      }
      return false;
    }

    this._isNotify = isNotify;

    // Glean metric names:
    // Glean.update.pingCountExternal
    // Glean.update.pingCountNotify
    // Glean.update.pingCountSubsequent
    Glean.update["pingCount" + this._pingSuffix].add();

    // Glean metric names:
    // Glean.update.unableToApplyExternal
    // Glean.update.unableToApplyNotify
    // Glean.update.unableToApplySubsequent
    if (!getCanApplyUpdates()) {
      Glean.update["unableToApply" + this._pingSuffix].add();
    }

    // Glean metric names:
    // Glean.update.cannotStageExternal
    // Glean.update.cannotStageNotify
    // Glean.update.cannotStageSubsequent
    if (!getCanApplyUpdates()) {
      Glean.update["cannotStage" + this._pingSuffix].add();
    }
    if (AppConstants.platform == "win") {
      // labeled counter metric names:
      // Glean.update.canUseBitsExternal
      // Glean.update.canUseBitsNotify
      // Glean.update.canUseBitsSubsequent
      Glean.update["canUseBits" + this._pingSuffix][getCanUseBits()].add();
    }
    // Glean metric names:
    // Glean.update.invalidLastupdatetimeExternal
    // Glean.update.invalidLastupdatetimeNotify
    // Glean.update.invalidLastupdatetimeSubsequent
    // Glean.update.lastNotifyIntervalDaysExternal
    // Glean.update.lastNotifyIntervalDaysNotify
    // Glean.update.lastNotifyIntervalDaysSubsequent
    AUSTLMY.pingLastUpdateTime(this._pingSuffix);
    // Glean metric names:
    // Glean.update.notPrefUpdateAutoExternal
    // Glean.update.notPrefUpdateAutoNotify
    // Glean.update.notPrefUpdateAutoSubsequent
    lazy.UpdateUtils.getAppUpdateAutoEnabled().then(enabled => {
      if (!enabled) {
        Glean.update["notPrefUpdateAuto" + this._pingSuffix].add();
      }
    });
    // Glean metric names:
    // Glean.update.notPrefUpdateStagingEnabledExternal
    // Glean.update.notPrefUpdateStagingEnabledNotify
    // Glean.update.notPrefUpdateStagingEnabledSubsequent
    AUSTLMY.pingBoolPref(
      Glean.update["notPrefUpdateStagingEnabled" + this._pingSuffix],
      PREF_APP_UPDATE_STAGING_ENABLED,
      true,
      true
    );
    if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
      // Glean metric names:
      // Glean.update.prefUpdateCancelationsExternal
      // Glean.update.prefUpdateCancelationsNotify
      // Glean.update.prefUpdateCancelationsSubsequent
      AUSTLMY.pingIntPref(
        Glean.update["prefUpdateCancelations" + this._pingSuffix],
        PREF_APP_UPDATE_CANCELATIONS,
        0,
        0
      );
    }
    if (AppConstants.MOZ_MAINTENANCE_SERVICE) {
      // Glean metric names:
      // Glean.update.notPrefUpdateServiceEnabledExternal
      // Glean.update.notPrefUpdateServiceEnabledNotify
      // Glean.update.notPrefUpdateServiceEnabledSubsequent
      AUSTLMY.pingBoolPref(
        Glean.update["notPrefUpdateServiceEnabled" + this._pingSuffix],
        PREF_APP_UPDATE_SERVICE_ENABLED,
        true
      );
      // Glean metric names:
      // Glean.update.prefServiceErrorsExternal
      // Glean.update.prefServiceErrorsNotify
      // Glean.update.prefServiceErrorsSubsequent
      AUSTLMY.pingIntPref(
        Glean.update["prefServiceErrors" + this._pingSuffix],
        PREF_APP_UPDATE_SERVICE_ERRORS,
        0,
        0
      );
      if (AppConstants.platform == "win") {
        // Glean metric names:
        // Glean.update.serviceInstalledExternal
        // Glean.update.serviceInstalledNotify
        // Glean.update.serviceInstalledSubsequent
        // Glean.update.serviceManuallyUninstalledExternal
        // Glean.update.serviceManuallyUninstalledNotify
        // Glean.update.serviceManuallyUninstalledSubsequent
        AUSTLMY.pingServiceInstallStatus(
          this._pingSuffix,
          isServiceInstalled()
        );
      }
    }

    // If a download is in progress or the patch has been staged do nothing.
    if (this.isDownloading) {
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_IS_DOWNLOADING);
      return false;
    }

    // Once we have downloaded a complete update, do not download further
    // updates until the complete update is installed. This is important,
    // because if we fall back from a partial update to a complete update,
    // it might be because of changes to the patch directory (which would cause
    // a failure to apply any partial MAR). So we really don't want to replace
    // a downloaded complete update with a downloaded partial update. And we
    // do not currently download complete updates if there is already a
    // readyUpdate available.
    if (
      lazy.UM.internal.readyUpdate &&
      lazy.UM.internal.readyUpdate.selectedPatch &&
      lazy.UM.internal.readyUpdate.selectedPatch.type == "complete"
    ) {
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_IS_DOWNLOADED);
      return false;
    }

    // If we start downloading an update while the readyUpdate is staging, we
    // run the risk of eventually wanting to overwrite readyUpdate with the
    // downloadingUpdate while the readyUpdate is still staging. Then we would
    // have to have a weird intermediate state where the downloadingUpdate has
    // finished downloading, but can't be moved yet. It's simpler to just not
    // start a new update if the old one is still staging.
    if (this.currentState == Ci.nsIApplicationUpdateService.STATE_STAGING) {
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_IS_DOWNLOADED);
      return false;
    }

    // Asynchronously kick off update checking
    (async () => {
      let validUpdateURL = true;
      try {
        await lazy.CheckSvc.getUpdateURL(lazy.CheckSvc.BACKGROUND_CHECK);
      } catch (e) {
        validUpdateURL = false;
      }

      // The following checks are done here so they can be differentiated from
      // foreground checks.
      if (!lazy.UpdateUtils.OSVersion) {
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_NO_OS_VERSION);
      } else if (!lazy.UpdateUtils.ABI) {
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_NO_OS_ABI);
      } else if (!validUpdateURL) {
        AUSTLMY.pingCheckCode(
          this._pingSuffix,
          AUSTLMY.CHK_INVALID_DEFAULT_URL
        );
      } else if (!hasUpdateMutex()) {
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_NO_MUTEX);
      } else if (isOtherInstanceRunning()) {
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_OTHER_INSTANCE);
      } else if (!this.canCheckForUpdates) {
        AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_UNABLE_TO_CHECK);
      }

      let check = lazy.CheckSvc.checkForUpdates(lazy.CheckSvc.BACKGROUND_CHECK);
      await this.onCheckComplete(await check.result);
    })();

    return true;
  }

  /**
   * Determine the update from the specified updates that should be offered.
   * If both valid major and minor updates are available the minor update will
   * be offered.
   * @param   updates
   *          An array of available nsIUpdate items
   * @return  The nsIUpdate to offer.
   */
  #selectUpdate(updates) {
    if (!updates.length) {
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_NO_UPDATE_FOUND);
      return null;
    }

    // The ping for unsupported is sent after the call to showPrompt.
    if (updates.length == 1 && updates[0].unsupported) {
      return updates[0];
    }

    // Choose the newest of the available minor and major updates.
    var majorUpdate = null;
    var minorUpdate = null;
    var vc = Services.vc;
    let lastCheckCode = AUSTLMY.CHK_NO_COMPAT_UPDATE_FOUND;

    for (const update of updates) {
      // Ignore updates for older versions of the application and updates for
      // the same version of the application with the same build ID.
      if (updateIsAtLeastAsOldAsCurrentVersion(update)) {
        LOG(
          "UpdateService:selectUpdate - skipping update because the " +
            "update's application version is not greater than the current " +
            "application version"
        );
        lastCheckCode = AUSTLMY.CHK_UPDATE_PREVIOUS_VERSION;
        continue;
      }

      if (updateIsAtLeastAsOldAsReadyUpdate(update)) {
        LOG(
          "UpdateService:selectUpdate - skipping update because the " +
            "update's application version is not greater than that of the " +
            "currently downloaded update"
        );
        lastCheckCode = AUSTLMY.CHK_UPDATE_PREVIOUS_VERSION;
        continue;
      }

      if (lazy.UM.internal.readyUpdate && !getPatchOfType(update, "partial")) {
        LOG(
          "UpdateService:selectUpdate - skipping update because no partial " +
            "patch is available and an update has already been downloaded."
        );
        lastCheckCode = AUSTLMY.CHK_NO_PARTIAL_PATCH;
        continue;
      }

      switch (update.type) {
        case "major":
          if (!majorUpdate || majorUpdate.unsupported) {
            majorUpdate = update;
          } else if (
            !update.unsupported &&
            vc.compare(majorUpdate.appVersion, update.appVersion) <= 0
          ) {
            majorUpdate = update;
          }
          break;
        case "minor":
          if (!minorUpdate || minorUpdate.unsupported) {
            minorUpdate = update;
          } else if (
            !update.unsupported &&
            vc.compare(minorUpdate.appVersion, update.appVersion) <= 0
          ) {
            minorUpdate = update;
          }
          break;
        default:
          LOG(
            "UpdateService:selectUpdate - skipping unknown update type: " +
              update.type
          );
          lastCheckCode = AUSTLMY.CHK_UPDATE_INVALID_TYPE;
          break;
      }
    }

    let update = minorUpdate || majorUpdate;
    if (AppConstants.platform == "macosx" && update) {
      if (getElevationRequired()) {
        let installAttemptVersion = Services.prefs.getCharPref(
          PREF_APP_UPDATE_ELEVATE_VERSION,
          null
        );
        if (vc.compare(installAttemptVersion, update.appVersion) != 0) {
          Services.prefs.setCharPref(
            PREF_APP_UPDATE_ELEVATE_VERSION,
            update.appVersion
          );
          if (
            Services.prefs.prefHasUserValue(PREF_APP_UPDATE_CANCELATIONS_OSX)
          ) {
            Services.prefs.clearUserPref(PREF_APP_UPDATE_CANCELATIONS_OSX);
          }
          if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_ELEVATE_NEVER)) {
            Services.prefs.clearUserPref(PREF_APP_UPDATE_ELEVATE_NEVER);
          }
        } else {
          let numCancels = Services.prefs.getIntPref(
            PREF_APP_UPDATE_CANCELATIONS_OSX,
            0
          );
          let rejectedVersion = Services.prefs.getCharPref(
            PREF_APP_UPDATE_ELEVATE_NEVER,
            ""
          );
          let maxCancels = Services.prefs.getIntPref(
            PREF_APP_UPDATE_CANCELATIONS_OSX_MAX,
            DEFAULT_CANCELATIONS_OSX_MAX
          );
          if (numCancels >= maxCancels) {
            LOG(
              "UpdateService:selectUpdate - the user requires elevation to " +
                "install this update, but the user has exceeded the max " +
                "number of elevation attempts."
            );
            update.elevationFailure = true;
            AUSTLMY.pingCheckCode(
              this._pingSuffix,
              AUSTLMY.CHK_ELEVATION_DISABLED_FOR_VERSION
            );
          } else if (vc.compare(rejectedVersion, update.appVersion) == 0) {
            LOG(
              "UpdateService:selectUpdate - the user requires elevation to " +
                "install this update, but elevation is disabled for this " +
                "version."
            );
            update.elevationFailure = true;
            AUSTLMY.pingCheckCode(
              this._pingSuffix,
              AUSTLMY.CHK_ELEVATION_OPTOUT_FOR_VERSION
            );
          } else {
            LOG(
              "UpdateService:selectUpdate - the user requires elevation to " +
                "install the update."
            );
          }
        }
      } else {
        // Clear elevation-related prefs since they no longer apply (the user
        // may have gained write access to the Firefox directory or an update
        // was executed with a different profile).
        if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_ELEVATE_VERSION)) {
          Services.prefs.clearUserPref(PREF_APP_UPDATE_ELEVATE_VERSION);
        }
        if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_CANCELATIONS_OSX)) {
          Services.prefs.clearUserPref(PREF_APP_UPDATE_CANCELATIONS_OSX);
        }
        if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_ELEVATE_NEVER)) {
          Services.prefs.clearUserPref(PREF_APP_UPDATE_ELEVATE_NEVER);
        }
      }
    } else if (!update) {
      AUSTLMY.pingCheckCode(this._pingSuffix, lastCheckCode);
    }

    return update;
  }

  /*
   * See nsIUpdateService.idl
   */
  async selectUpdate(updates) {
    await this.init();
    return this.#selectUpdate(updates);
  }

  /**
   * Determine which of the specified updates should be installed and begin the
   * download/installation process or notify the user about the update.
   * @param   updates
   *          An array of available updates
   */
  async _selectAndInstallUpdate(updates) {
    // Return early if there's an active update. The user is already aware and
    // is downloading or performed some user action to prevent notification.
    if (lazy.UM.internal.downloadingUpdate) {
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_HAS_ACTIVEUPDATE);
      return;
    }

    if (this.disabled) {
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_DISABLED_BY_POLICY);
      LOG(
        "UpdateService:_selectAndInstallUpdate - not prompting because " +
          "update is disabled"
      );
      return;
    }

    var update = this.#selectUpdate(updates);
    if (!update || update.elevationFailure) {
      return;
    }

    if (update.unsupported) {
      LOG(
        "UpdateService:_selectAndInstallUpdate - update not supported for " +
          "this system. Notifying observers. topic: update-available, " +
          "status: unsupported"
      );
      Services.obs.notifyObservers(update, "update-available", "unsupported");
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_UNSUPPORTED);
      return;
    }

    if (!getCanApplyUpdates()) {
      LOG(
        "UpdateService:_selectAndInstallUpdate - the user is unable to " +
          "apply updates... prompting. Notifying observers. " +
          "topic: update-available, status: cant-apply"
      );
      Services.obs.notifyObservers(null, "update-available", "cant-apply");
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_UNABLE_TO_APPLY);
      return;
    }

    /**
     * From this point on there are two possible outcomes:
     * 1. download and install the update automatically
     * 2. notify the user about the availability of an update
     *
     * Notes:
     * a) if the app.update.auto preference is false then automatic download and
     *    install is disabled and the user will be notified.
     *
     * If the update when it is first read does not have an appVersion attribute
     * the following deprecated behavior will occur:
     * Update Type   Outcome
     * Major         Notify
     * Minor         Auto Install
     */
    let updateAuto = await lazy.UpdateUtils.getAppUpdateAutoEnabled();
    if (!updateAuto) {
      LOG(
        "UpdateService:_selectAndInstallUpdate - prompting because silent " +
          "install is disabled. Notifying observers. topic: update-available, " +
          "status: show-prompt"
      );
      AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_SHOWPROMPT_PREF);
      Services.obs.notifyObservers(update, "update-available", "show-prompt");
      return;
    }

    LOG("UpdateService:_selectAndInstallUpdate - download the update");
    let result = await this.#downloadUpdate(update);
    if (
      result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS &&
      !this.isDownloading
    ) {
      LOG(
        "UpdateService:_selectAndInstallUpdate - Failed to start downloading " +
          "update. Cleaning up downloading update."
      );
      await cleanupDownloadingUpdate();
    }
    AUSTLMY.pingCheckCode(this._pingSuffix, AUSTLMY.CHK_DOWNLOAD_UPDATE);
  }

  /**
   * See nsIUpdateService.idl
   */
  get isAppBaseDirWritable() {
    return isAppBaseDirWritable();
  }

  get disabledForTesting() {
    return lazy.UpdateServiceStub.updateDisabledForTesting;
  }

  /**
   * See nsIUpdateService.idl
   */
  get disabled() {
    return lazy.UpdateServiceStub.updateDisabled;
  }

  /**
   * See nsIUpdateService.idl
   */
  get manualUpdateOnly() {
    return (
      Services.policies && !Services.policies.isAllowed("autoAppUpdateChecking")
    );
  }

  /**
   * See nsIUpdateService.idl
   */
  get canUsuallyCheckForUpdates() {
    if (this.disabled) {
      LOG(
        "UpdateService.canUsuallyCheckForUpdates - unable to automatically check " +
          "for updates, the option has been disabled by the administrator."
      );
      return false;
    }

    // If we don't know the binary platform we're updating, we can't update.
    if (!lazy.UpdateUtils.ABI) {
      LOG(
        "UpdateService.canUsuallyCheckForUpdates - unable to check for updates, " +
          "unknown ABI"
      );
      return false;
    }

    // If we don't know the OS version we're updating, we can't update.
    if (!lazy.UpdateUtils.OSVersion) {
      LOG(
        "UpdateService.canUsuallyCheckForUpdates - unable to check for updates, " +
          "unknown OS version"
      );
      return false;
    }

    LOG("UpdateService.canUsuallyCheckForUpdates - able to check for updates");
    return true;
  }

  /**
   * See nsIUpdateService.idl
   */
  get canCheckForUpdates() {
    if (!this.canUsuallyCheckForUpdates) {
      return false;
    }

    if (!hasUpdateMutex()) {
      LOG(
        "UpdateService.canCheckForUpdates - unable to check for updates, " +
          "unable to acquire update mutex"
      );
      return false;
    }

    if (isOtherInstanceRunning()) {
      // This doesn't block update checks, but we will have to wait until either
      // the other instance is gone or we time out waiting for it.
      LOG(
        "UpdateService.canCheckForUpdates - another instance is holding the " +
          "lock, will need to wait for it prior to checking for updates"
      );
    }

    LOG("UpdateService.canCheckForUpdates - able to check for updates");
    return true;
  }

  /**
   * See nsIUpdateService.idl
   */
  get elevationRequired() {
    return getElevationRequired();
  }

  /**
   * See nsIUpdateService.idl
   */
  get canUsuallyApplyUpdates() {
    return getCanApplyUpdates();
  }

  /**
   * See nsIUpdateService.idl
   */
  get canApplyUpdates() {
    return (
      this.canUsuallyApplyUpdates &&
      hasUpdateMutex() &&
      !isOtherInstanceRunning()
    );
  }

  /**
   * See nsIUpdateService.idl
   */
  get canUsuallyStageUpdates() {
    return getCanStageUpdates(false);
  }

  /**
   * See nsIUpdateService.idl
   */
  get canStageUpdates() {
    return getCanStageUpdates();
  }

  /**
   * See nsIUpdateService.idl
   */
  get canUsuallyUseBits() {
    return getCanUseBits(false) == "CanUseBits";
  }

  /**
   * See nsIUpdateService.idl
   */
  get canUseBits() {
    return getCanUseBits() == "CanUseBits";
  }

  /**
   * See nsIUpdateService.idl
   */
  get isOtherInstanceHandlingUpdates() {
    return !hasUpdateMutex() || isOtherInstanceRunning();
  }

  /**
   * A set of download listeners to be notified by this._downloader when it
   * receives nsIRequestObserver or nsIProgressEventSink method calls.
   *
   * These are stored on the UpdateService rather than on the Downloader,
   * because they ought to persist across multiple Downloader instances.
   */
  _downloadListeners = new Set();

  /**
   * See nsIUpdateService.idl
   */
  addDownloadListener(listener) {
    let oldSize = this._downloadListeners.size;
    this._downloadListeners.add(listener);

    if (this._downloadListeners.size == oldSize) {
      LOG(
        "UpdateService:addDownloadListener - Warning: Didn't add duplicate " +
          "listener"
      );
      return;
    }

    if (this._downloader) {
      this._downloader.onDownloadListenerAdded();
    }
  }

  /**
   * See nsIUpdateService.idl
   */
  removeDownloadListener(listener) {
    let elementRemoved = this._downloadListeners.delete(listener);

    if (!elementRemoved) {
      LOG(
        "UpdateService:removeDownloadListener - Warning: Didn't remove " +
          "non-existent listener"
      );
      return;
    }

    if (this._downloader) {
      this._downloader.onDownloadListenerRemoved();
    }
  }

  /**
   * Returns a boolean indicating whether there are any download listeners
   */
  get hasDownloadListeners() {
    return !!this._downloadListeners.length;
  }

  /*
   * Calls the provided function once with each download listener that is
   * currently registered.
   */
  forEachDownloadListener(fn) {
    // Make a shallow copy in case listeners remove themselves.
    let listeners = new Set(this._downloadListeners);
    listeners.forEach(fn);
  }

  /**
   * See nsIUpdateService.idl
   */
  async downloadUpdate(update) {
    await this.init();
    return this.#downloadUpdate(update);
  }

  async #downloadUpdate(update) {
    if (!update) {
      throw Components.Exception("", Cr.NS_ERROR_NULL_POINTER);
    }

    // Don't download the update if the update's version is less than the
    // current application's version or the update's version is the same as the
    // application's version and the build ID is the same as the application's
    // build ID. If we already have an update ready, we want to apply those
    // same checks against the version of the ready update, so that we don't
    // download an update that isn't newer than the one we already have.
    if (updateIsAtLeastAsOldAsCurrentVersion(update)) {
      LOG(
        "UpdateService:downloadUpdate - Skipping download of update since " +
          "it is for an earlier or same application version and build ID.\n" +
          "current application version: " +
          Services.appinfo.version +
          "\n" +
          "update application version : " +
          update.appVersion +
          "\n" +
          "current build ID: " +
          Services.appinfo.appBuildID +
          "\n" +
          "update build ID : " +
          update.buildID
      );
      return Ci.nsIApplicationUpdateService.DOWNLOAD_FAILURE_GENERIC;
    }
    if (updateIsAtLeastAsOldAsReadyUpdate(update)) {
      LOG(
        "UpdateService:downloadUpdate - not downloading update because the " +
          "update that's already been downloaded is the same version or " +
          "newer.\n" +
          "currently downloaded update application version: " +
          lazy.UM.internal.readyUpdate.appVersion +
          "\n" +
          "available update application version : " +
          update.appVersion +
          "\n" +
          "currently downloaded update build ID: " +
          lazy.UM.internal.readyUpdate.buildID +
          "\n" +
          "available update build ID : " +
          update.buildID
      );
      return Ci.nsIApplicationUpdateService.DOWNLOAD_FAILURE_GENERIC;
    }

    // If a download request is in progress vs. a download ready to resume
    if (this.isDownloading) {
      if (update.isCompleteUpdate == this._downloader.isCompleteUpdate) {
        LOG(
          "UpdateService:downloadUpdate - no support for downloading more " +
            "than one update at a time"
        );
        return Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS;
      }
      this._downloader.cancel();
    }
    this._downloader = new Downloader(this);
    return this._downloader.downloadUpdate(update);
  }

  /**
   * See nsIUpdateService.idl
   */
  async stopDownload() {
    await this.init();
    return this.#stopDownload();
  }

  async #stopDownload() {
    if (this.isDownloading) {
      await this._downloader.cancel();
    } else if (this._retryTimer) {
      // Download status is still considered as 'downloading' during retry.
      // We need to cancel both retry and download at this stage.
      this._retryTimer.cancel();
      this._retryTimer = null;
      if (this._downloader) {
        await this._downloader.cancel();
      }
    }
    if (this._downloader) {
      await this._downloader.cleanup();
    }
    this._downloader = null;
  }

  /**
   * Note that this is different from checking if `currentState` is
   * `STATE_DOWNLOADING` because if we are downloading a second update, this
   * will be `true` while `currentState` will be `STATE_PENDING`.
   */
  get isDownloading() {
    return this._downloader && this._downloader.isBusy;
  }

  _logStatus() {
    if (!lazy.UpdateLog.enabled) {
      return;
    }
    if (this.disabled) {
      LOG("Current UpdateService status: disabled");
      // Return early if UpdateService is disabled by policy. Otherwise some of
      // the getters we call to display status information may discover that the
      // update directory is not writable, which automatically results in the
      // permissions being fixed. Which we shouldn't really be doing if update
      // is disabled by policy.
      return;
    }
    LOG("Logging current UpdateService status:");
    // These getters print their own logging
    this.canCheckForUpdates;
    this.canApplyUpdates;
    this.canStageUpdates;
    LOG("Elevation required: " + this.elevationRequired);
    LOG(
      "Other instance of the application currently running: " +
        this.isOtherInstanceHandlingUpdates
    );
    LOG("Current update state: " + this.getStateName(gUpdateState));
    LOG("Downloading: " + !!this.isDownloading);
    if (this._downloader && this._downloader.isBusy) {
      LOG("Downloading complete update: " + this._downloader.isCompleteUpdate);
      LOG("Downloader using BITS: " + this._downloader.usingBits);
      if (this._downloader._patch) {
        // This will print its own logging
        this._downloader._canUseBits(this._downloader._patch);

        // Downloader calls QueryInterface(Ci.nsIWritablePropertyBag) on
        // its _patch member as soon as it is assigned, so no need to do so
        // again here.
        let bitsResult = this._downloader._patch.getProperty("bitsResult");
        if (bitsResult != null) {
          LOG("Patch BITS result: " + bitsResult);
        }
        let internalResult =
          this._downloader._patch.getProperty("internalResult");
        if (internalResult != null) {
          LOG("Patch nsIIncrementalDownload result: " + internalResult);
        }
      }
    }
    LOG("End of UpdateService status");
  }

  /**
   * See nsIUpdateService.idl
   */
  get onlyDownloadUpdatesThisSession() {
    return gOnlyDownloadUpdatesThisSession;
  }

  /**
   * See nsIUpdateService.idl
   */
  set onlyDownloadUpdatesThisSession(newValue) {
    gOnlyDownloadUpdatesThisSession = newValue;
  }

  /**
   * See nsIUpdateService.idl
   */
  getStateName(state) {
    switch (state) {
      case Ci.nsIApplicationUpdateService.STATE_IDLE:
        return "STATE_IDLE";
      case Ci.nsIApplicationUpdateService.STATE_DOWNLOADING:
        return "STATE_DOWNLOADING";
      case Ci.nsIApplicationUpdateService.STATE_STAGING:
        return "STATE_STAGING";
      case Ci.nsIApplicationUpdateService.STATE_PENDING:
        return "STATE_PENDING";
      case Ci.nsIApplicationUpdateService.STATE_SWAP:
        return "STATE_SWAP";
    }
    return `[unknown update state: ${state}]`;
  }

  /**
   * See nsIUpdateService.idl
   */
  get currentState() {
    return gUpdateState;
  }

  /**
   * See nsIUpdateService.idl
   */
  get stateTransition() {
    return gStateTransitionPromise.promise;
  }

  /**
   * Either starts a BITS transfer job or connects to an existing one.
   * When starting a job, it starts it with a name and path that make sense for
   * an update MAR download.
   *
   * @param  parameters
   *         A parameters object must be passed, in which `bitsId` or `url` must
   *         be specified. If `bitsId` is specified (and not null), this will
   *         connect to an existing transfer.
   *           activeListeners
   *             If `true`, this option specifies that there are active
   *             listeners, so the faster "active" progress update polling rate
   *             should be used.
   *           bitsId
   *             The ID of the job to connect to. If this is not passed, a new
   *             transfer will be started.
   *           observer
   *             If specified, should be an instance of `nsIRequestObserver`
   *             and, optionally, `nsIProgressEventSink`. This will be connected
   *             to the `BitsRequest` that is returned.
   *           url
   *             The URL to download.
   *           extraHeaders
   *             String of extra headers to include, in the format accepted by
   *             `IBackgroundCopyJobHttpOptions::SetCustomHeaders`: separated by
   *             `\r\n`, terminated by an additional `\r\n`.
   * @return Promise<BitsRequest>
   *         Returns a request object
   * @throws BitsError
   *         On failure to connect to the BITS job.
   */
  async makeBitsRequest({
    activeListeners = false,
    bitsId,
    observer,
    url,
    extraHeaders,
  }) {
    let noProgressTimeout = BITS_IDLE_NO_PROGRESS_TIMEOUT_SECS;
    let monitorInterval = BITS_IDLE_POLL_RATE_MS;
    // The monitor's timeout should be much greater than the longest monitor
    // poll interval. If the timeout is too short, delay in the pipe to the
    // update agent might cause BITS to falsely report an error, causing an
    // unnecessary fallback to nsIIncrementalDownload.
    let monitorTimeout = Math.max(10 * monitorInterval, 10 * 60 * 1000);
    if (activeListeners) {
      noProgressTimeout = BITS_ACTIVE_NO_PROGRESS_TIMEOUT_SECS;
      monitorInterval = BITS_ACTIVE_POLL_RATE_MS;
    }

    let updateRootDir = FileUtils.getDir(KEY_UPDROOT, []);
    try {
      updateRootDir.create(
        Ci.nsIFile.DIRECTORY_TYPE,
        FileUtils.PERMS_DIRECTORY
      );
    } catch (ex) {
      if (ex.result != Cr.NS_ERROR_FILE_ALREADY_EXISTS) {
        throw ex;
      }
      // Ignore the exception due to a directory that already exists.
    }

    let jobName = "MozillaUpdate " + updateRootDir.leafName;
    let updatePath = getDownloadingUpdateDir().path;
    if (!Bits.initialized) {
      Bits.init(jobName, updatePath, monitorTimeout);
    }

    if (bitsId) {
      LOG(
        "UpdateService:makeBitsRequest - Connecting to in-progress download. " +
          "BITS ID: " +
          bitsId
      );

      return Bits.monitorDownload(bitsId, monitorInterval, observer, null);
    }

    LOG(
      "UpdateService:makeBitsRequest - Starting BITS download with url: " +
        url +
        ", updateDir: " +
        updatePath +
        ", filename: " +
        FILE_UPDATE_MAR
    );

    return Bits.startDownload(
      url,
      FILE_UPDATE_MAR,
      Ci.nsIBits.PROXY_PRECONFIG,
      noProgressTimeout,
      monitorInterval,
      extraHeaders,
      observer,
      null
    );
  }

  /**
   * Get rid of a downloading update. This is generally done before cleaning it
   * up.
   *
   * Connects to the in-progress BITS job in order to cancel it, if necessary.
   * This is generally only necessary when cancelling a job at startup,
   * otherwise we would have connected to it already.
   *
   * If there is not an in-progress download, this has no effect.
   */
  async cancelDownloadingUpdate() {
    if (this.isDownloading) {
      LOG(
        "UpdateService:cancelDownloadingUpdate - Job is connected already. Stopping."
      );
      await this.internal.stopDownload();
      return;
    }

    // If we didn't return above, either we haven't connected to the BITS job
    // yet or there is no BITS job and nothing to cancel.

    if (!lazy.UM.internal.downloadingUpdate) {
      LOG(
        "UpdateService:cancelDownloadingUpdate - Not cleaning up BITS Job. No update."
      );
      return;
    }
    const patch = lazy.UM.internal.downloadingUpdate.selectedPatch;
    if (!patch || !patch.QueryInterface(Ci.nsIWritablePropertyBag)) {
      LOG(
        "UpdateService:cancelDownloadingUpdate - Not cleaning up BITS Job. No patch."
      );
      return;
    }
    const bitsId = patch.getProperty("bitsId");
    if (!bitsId) {
      LOG(
        "UpdateService:cancelDownloadingUpdate - Not cleaning up BITS Job. No BITS ID."
      );
      return;
    }
    // If `!this.isDownloading`, we are not connected to the BITS request, which
    // we need to do to stop a BITS download.
    try {
      const request = await this.makeBitsRequest({ bitsId });
      await request.cancelAsync();
    } catch (ex) {
      LOG(
        `UpdateService:cancelDownloadingUpdate - Failed to clean up BITS Job ${bitsId}: ${ex}`
      );
      return;
    }
    LOG(
      `UpdateService:cancelDownloadingUpdate - Successfully cleaned up BITS Job ${bitsId}`
    );
  }

  classID = UPDATESERVICE_CID;

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIApplicationUpdateService,
    Ci.nsITimerCallback,
    Ci.nsIObserver,
  ]);
}

export class UpdateManager {
  /**
   * The nsIUpdate object for the update that has been downloaded.
   */
  _readyUpdate = null;

  /**
   * The nsIUpdate object for the update currently being downloaded.
   */
  _downloadingUpdate = null;

  /**
   * Whether the update history stored in _updates has changed since it was
   * loaded.
   */
  _updatesDirty = false;

  /**
   * The backing for `nsIUpdateManager.updateInstalledAtStartup`.
   */
  #updateInstalledAtStartup = null;

  /**
   * A service to manage active and past updates.
   * @constructor
   */
  constructor() {
    this.internal = {
      reload: async skipFiles => this.#reload(skipFiles),
      getHistory: () => this.#getHistory(),
      addUpdateToHistory: update => this.#addUpdateToHistory(update),
      refreshUpdateStatus: async () => this.#refreshUpdateStatus(),
      QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateManagerInternal]),
    };

    Object.defineProperty(this.internal, "readyUpdate", {
      get: () => this._readyUpdate,
      set: update => {
        this._readyUpdate = update;
      },
    });

    Object.defineProperty(this.internal, "downloadingUpdate", {
      get: () => this._downloadingUpdate,
      set: update => {
        this._downloadingUpdate = update;
      },
    });
  }

  /**
   * See `nsIUpdateManagerInternal.reload` in nsIUpdateService.idl
   */
  async #reload(skipFiles) {
    LOG("UpdateManager:#reload - Reloading update data.");
    if (this._updatesXMLSaver) {
      this._updatesXMLSaver.disarm();
    }

    let updates = [];
    this._updatesDirty = skipFiles;
    this._readyUpdate = null;
    this._downloadingUpdate = null;
    this.#updateInstalledAtStartup = null;
    transitionState(Ci.nsIApplicationUpdateService.STATE_IDLE);
    if (!skipFiles) {
      // Load the active-update.xml file to see if there is an active update.
      let activeUpdates = this._loadXMLFileIntoArray(FILE_ACTIVE_UPDATE_XML);
      if (activeUpdates.length) {
        const status = readStatusFile(getReadyUpdateDir());

        // If there are two updates, the first one is the ready update.
        // If there is only 1 update, we don't know which is which. We use the
        // state to figure it out.
        if (activeUpdates.length > 1) {
          this._readyUpdate = activeUpdates[0];
          this._downloadingUpdate = activeUpdates[1];
          if (activeUpdates.length > 2) {
            LOG(
              "UpdateManager:#reload - Warning: Ignoring additional (>2) " +
                "unexpected active updates"
            );
          }
        } else if (
          status == STATE_DOWNLOADING ||
          activeUpdates[0].state == STATE_DOWNLOADING
        ) {
          this._downloadingUpdate = activeUpdates[0];
          transitionState(Ci.nsIApplicationUpdateService.STATE_DOWNLOADING);
        } else {
          this._readyUpdate = activeUpdates[0];
        }

        LOG(`UpdateManager:#reload - Got status = ${status}`);
        if (
          [
            STATE_PENDING,
            STATE_PENDING_SERVICE,
            STATE_PENDING_ELEVATE,
            STATE_APPLIED,
            STATE_APPLIED_SERVICE,
          ].includes(status)
        ) {
          transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
        }
        if (status == STATE_SUCCEEDED && this._readyUpdate) {
          this.#updateInstalledAtStartup = this._readyUpdate;
          // Bug 1889785 - When deciding whether or not to show a What's New Page, we
          // rely on both `Services.appinfo.platformVersion` and this value. But Balrog
          // doesn't guarantee that the value that it sends will match appinfo.
          // We synchronize the values here so they are consistent.
          this.#updateInstalledAtStartup.platformVersion =
            Services.appinfo.platformVersion;
          this.saveUpdates();
        }
      }
      updates = this._loadXMLFileIntoArray(FILE_UPDATES_XML);
    }
    this._updatesCache = updates;

    LOG(
      "UpdateManager:#reload - Reloaded downloadingUpdate as " +
        this._downloadingUpdate
    );
    if (this._downloadingUpdate) {
      LOG(
        "UpdateManager:#reload - Reloaded downloadingUpdate state as " +
          this._downloadingUpdate.state
      );
    }
    LOG("UpdateManager:#reload - Reloaded readyUpdate as " + this._readyUpdate);
    if (this._readyUpdate) {
      LOG(
        "UpdateManager:#reload - Reloaded readyUpdate state as " +
          this._readyUpdate.state
      );
    }
    LOG(
      "UpdateManager:UpdateManager - Reloaded updateInstalledAtStartup as  " +
        this.#updateInstalledAtStartup
    );
  }

  /**
   * Loads an updates.xml formatted file into an array of nsIUpdate items.
   * @param   fileName
   *          The file name in the updates directory to load.
   * @return  The array of nsIUpdate items held in the file.
   */
  _loadXMLFileIntoArray(fileName) {
    let updates = [];
    let file = getUpdateFile([fileName]);
    if (!file.exists()) {
      LOG(
        "UpdateManager:_loadXMLFileIntoArray - XML file does not exist. " +
          "path: " +
          file.path
      );
      return updates;
    }

    // Open the active-update.xml file with both read and write access so
    // opening it will fail if it isn't possible to also write to the file. When
    // opening it fails it means that it isn't possible to update and the code
    // below will return early without loading the active-update.xml. This will
    // also make it so notifications to update manually will still be shown.
    let mode =
      fileName == FILE_ACTIVE_UPDATE_XML
        ? FileUtils.MODE_RDWR
        : FileUtils.MODE_RDONLY;
    let fileStream = Cc[
      "@mozilla.org/network/file-input-stream;1"
    ].createInstance(Ci.nsIFileInputStream);
    try {
      fileStream.init(file, mode, FileUtils.PERMS_FILE, 0);
    } catch (e) {
      LOG(
        "UpdateManager:_loadXMLFileIntoArray - error initializing file " +
          "stream. Exception: " +
          e
      );
      return updates;
    }
    try {
      var parser = new DOMParser();
      var doc = parser.parseFromStream(
        fileStream,
        "UTF-8",
        fileStream.available(),
        "text/xml"
      );

      var updateCount = doc.documentElement.childNodes.length;
      for (var i = 0; i < updateCount; ++i) {
        var updateElement = doc.documentElement.childNodes.item(i);
        if (
          updateElement.nodeType != updateElement.ELEMENT_NODE ||
          updateElement.localName != "update"
        ) {
          continue;
        }

        let update;
        try {
          update = new Update(updateElement);
        } catch (e) {
          LOG("UpdateManager:_loadXMLFileIntoArray - invalid update");
          continue;
        }
        updates.push(update);
      }
    } catch (ex) {
      LOG(
        "UpdateManager:_loadXMLFileIntoArray - error constructing update " +
          "list. Exception: " +
          ex
      );
    }
    fileStream.close();
    if (!updates.length) {
      LOG(
        "UpdateManager:_loadXMLFileIntoArray - update xml file " +
          fileName +
          " exists but doesn't contain any updates"
      );
      // The file exists but doesn't contain any updates so remove it.
      try {
        file.remove(false);
      } catch (e) {
        LOG(
          "UpdateManager:_loadXMLFileIntoArray - error removing " +
            fileName +
            " file. Exception: " +
            e
        );
      }
    }
    return updates;
  }

  #getHistory() {
    const history = this._getUpdates();
    return [...history];
  }

  /**
   * Loads the update history from the updates.xml file into a cache.
   */
  _getUpdates() {
    if (!this._updatesCache) {
      this._updatesCache = this._loadXMLFileIntoArray(FILE_UPDATES_XML);
    }
    return this._updatesCache;
  }

  /**
   * See nsIUpdateService.idl
   */
  async getHistory() {
    await lazy.AUS.init();
    return lazy.UM.internal.getHistory();
  }

  /**
   * See nsIUpdateService.idl
   */
  async getReadyUpdate() {
    await lazy.AUS.init();
    return this._readyUpdate;
  }

  /**
   * See nsIUpdateService.idl
   */
  async getDownloadingUpdate() {
    await lazy.AUS.init();
    return this._downloadingUpdate;
  }

  /**
   * See nsIUpdateService.idl
   */
  async updateInstalledAtStartup() {
    await lazy.AUS.init();
    return this.#updateInstalledAtStartup;
  }

  /**
   * See nsIUpdateService.idl
   */
  async lastUpdateInstalled() {
    await lazy.AUS.init();
    if (this.#updateInstalledAtStartup) {
      return this.#updateInstalledAtStartup;
    }
    return this._getUpdates().find(u => u.state == STATE_SUCCEEDED) ?? null;
  }

  #addUpdateToHistory(aUpdate) {
    this._updatesDirty = true;
    let updates = this._getUpdates();
    updates.unshift(aUpdate);
    // Limit the update history to 10 updates.
    updates.splice(10);
  }

  /**
   * See nsIUpdateService.idl
   */
  async addUpdateToHistory(aUpdate) {
    await lazy.AUS.init();
    this.#addUpdateToHistory(aUpdate);
  }

  /**
   * Serializes an array of updates to an XML file or removes the file if the
   * array length is 0.
   * @param   updates
   *          An array of nsIUpdate objects
   * @param   fileName
   *          The file name in the updates directory to write to.
   * @return  true on success, false on error
   */
  async _writeUpdatesToXMLFile(updates, fileName) {
    let file;
    try {
      file = getUpdateFile([fileName]);
    } catch (e) {
      LOG(
        "UpdateManager:_writeUpdatesToXMLFile - Unable to get XML file - " +
          "Exception: " +
          e
      );
      return false;
    }
    if (!updates.length) {
      LOG(
        "UpdateManager:_writeUpdatesToXMLFile - no updates to write. " +
          "removing file: " +
          file.path
      );
      try {
        await IOUtils.remove(file.path);
      } catch (e) {
        LOG(
          "UpdateManager:_writeUpdatesToXMLFile - Delete file exception: " + e
        );
        return false;
      }
      return true;
    }

    const EMPTY_UPDATES_DOCUMENT_OPEN =
      '<?xml version="1.0"?><updates xmlns="' + URI_UPDATE_NS + '">';
    const EMPTY_UPDATES_DOCUMENT_CLOSE = "</updates>";
    try {
      var parser = new DOMParser();
      var doc = parser.parseFromString(
        EMPTY_UPDATES_DOCUMENT_OPEN + EMPTY_UPDATES_DOCUMENT_CLOSE,
        "text/xml"
      );

      for (var i = 0; i < updates.length; ++i) {
        doc.documentElement.appendChild(updates[i].serialize(doc));
      }

      var xml =
        EMPTY_UPDATES_DOCUMENT_OPEN +
        doc.documentElement.innerHTML +
        EMPTY_UPDATES_DOCUMENT_CLOSE;
      // If the destination file existed and is removed while the following is
      // being performed the copy of the tmp file to the destination file will
      // fail.
      await IOUtils.writeUTF8(file.path, xml, {
        tmpPath: file.path + ".tmp",
      });
      await IOUtils.setPermissions(file.path, FileUtils.PERMS_FILE);
    } catch (e) {
      LOG("UpdateManager:_writeUpdatesToXMLFile - Exception: " + e);
      return false;
    }
    return true;
  }

  _updatesXMLSaver = null;
  _updatesXMLSaverCallback = null;
  /**
   * See nsIUpdateService.idl
   */
  saveUpdates() {
    if (!this._updatesXMLSaver) {
      this._updatesXMLSaverCallback = () => this._updatesXMLSaver.finalize();

      this._updatesXMLSaver = new lazy.DeferredTask(
        () => this._saveUpdatesXML(),
        XML_SAVER_INTERVAL_MS
      );
      lazy.AsyncShutdown.profileBeforeChange.addBlocker(
        "UpdateManager: writing update xml data",
        this._updatesXMLSaverCallback
      );
    } else {
      this._updatesXMLSaver.disarm();
    }

    this._updatesXMLSaver.arm();
  }

  /**
   * Saves the active-updates.xml and updates.xml when the updates history has
   * been modified files.
   */
  _saveUpdatesXML() {
    // This mechanism for how we store the updates might seem a bit odd, since,
    // if only one update is stored, we don't know if it's the ready update or
    // the downloading update. However, we can determine which it is by reading
    // update.status. If we read STATE_DOWNLOADING, it must be a downloading
    // update and otherwise it's a ready update. This method has the additional
    // advantage of requiring no migration from when we used to only store a
    // single active update.
    let updates = [];
    if (this._readyUpdate) {
      updates.push(this._readyUpdate);
    }
    if (this._downloadingUpdate) {
      updates.push(this._downloadingUpdate);
    }

    // The active update stored in the active-update.xml file will change during
    // the lifetime of an active update and the file should always be updated
    // when saveUpdates is called.
    let promises = [];
    promises[0] = this._writeUpdatesToXMLFile(updates, FILE_ACTIVE_UPDATE_XML);
    // The update history stored in the updates.xml file should only need to be
    // updated when an active update has been added to it in which case
    // |_updatesDirty| will be true.
    if (this._updatesDirty) {
      this._updatesDirty = false;
      promises[1] = this._writeUpdatesToXMLFile(
        this._getUpdates(),
        FILE_UPDATES_XML
      );
    }
    return Promise.all(promises);
  }

  /**
   * See nsIUpdateService.idl
   */
  async refreshUpdateStatus() {
    return this.#refreshUpdateStatus();
  }

  async #refreshUpdateStatus() {
    try {
      LOG("UpdateManager:refreshUpdateStatus - Staging done.");

      await lazy.AUS.init();

      var update = this._readyUpdate;
      if (!update) {
        LOG("UpdateManager:refreshUpdateStatus - Missing ready update?");
        return;
      }

      var status = readStatusFile(getReadyUpdateDir());
      pingStateAndStatusCodes(update, false, status);
      LOG(`UpdateManager:refreshUpdateStatus - status = ${status}`);

      let parts = status.split(":");
      update.state = parts[0];
      if (update.state == STATE_APPLYING) {
        LOG(
          "UpdateManager:refreshUpdateStatus - Staging appears to have crashed."
        );
        update.state = STATE_FAILED;
        update.errorCode = ERR_UPDATER_CRASHED;
      } else if (update.state == STATE_FAILED) {
        LOG("UpdateManager:refreshUpdateStatus - Staging failed.");
        if (parts[1]) {
          update.errorCode = parseInt(parts[1]) || INVALID_UPDATER_STATUS_CODE;
        } else {
          update.errorCode = INVALID_UPDATER_STATUS_CODE;
        }
      }

      // Rotate the update logs so the update log isn't removed if a complete
      // update is downloaded. By passing false the patch directory won't be
      // removed.
      cleanUpReadyUpdateDir(false);

      if (update.state == STATE_FAILED) {
        let isMemError = isMemoryAllocationErrorCode(update.errorCode);
        if (
          update.errorCode == DELETE_ERROR_STAGING_LOCK_FILE ||
          update.errorCode == UNEXPECTED_STAGING_ERROR ||
          isMemError
        ) {
          update.state = getBestPendingState();
          writeStatusFile(getReadyUpdateDir(), update.state);
          if (isMemError) {
            LOG(
              `UpdateManager:refreshUpdateStatus - Updater failed to ` +
                `allocate enough memory to successfully stage. Setting ` +
                `status to "${update.state}"`
            );
          } else {
            LOG(
              `UpdateManager:refreshUpdateStatus - Unexpected staging error. ` +
                `Setting status to "${update.state}"`
            );
          }
        } else if (isServiceSpecificErrorCode(update.errorCode)) {
          // Sometimes when staging, we might encounter an error that is
          // specific to the Maintenance Service. If this happens, we should try
          // to update without the Service.
          LOG(
            `UpdateManager:refreshUpdateStatus - Encountered service ` +
              `specific error code: ${update.errorCode}. Will try installing ` +
              `update without the Maintenance Service. Setting state to pending`
          );
          update.state = STATE_PENDING;
          writeStatusFile(getReadyUpdateDir(), update.state);
        } else {
          LOG(
            "UpdateManager:refreshUpdateStatus - Attempting handleUpdateFailure"
          );
          if (!handleUpdateFailure(update)) {
            LOG(
              "UpdateManager:refreshUpdateStatus - handleUpdateFailure " +
                "failed. Attempting to fall back to complete update."
            );
            await handleFallbackToCompleteUpdate();
          }
        }
      }
      if (update.state == STATE_APPLIED && shouldUseService()) {
        LOG(
          `UpdateManager:refreshUpdateStatus - Staging successful. ` +
            `Setting status to "${STATE_APPLIED_SERVICE}"`
        );
        writeStatusFile(
          getReadyUpdateDir(),
          (update.state = STATE_APPLIED_SERVICE)
        );
      }

      // Now that the active update's properties have been updated write the
      // active-update.xml to disk. Since there have been no changes to the
      // update history the updates.xml will not be written to disk.
      this.saveUpdates();

      // Send an observer notification which the app update doorhanger uses to
      // display a restart notification after any langpacks have staged.
      await promiseLangPacksUpdated(update);

      if (
        update.state == STATE_APPLIED ||
        update.state == STATE_APPLIED_SERVICE ||
        update.state == STATE_PENDING ||
        update.state == STATE_PENDING_SERVICE ||
        update.state == STATE_PENDING_ELEVATE
      ) {
        LOG("UpdateManager:refreshUpdateStatus - Setting state STATE_PENDING");
        transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
      }

      LOG(
        "UpdateManager:refreshUpdateStatus - Notifying observers that " +
          "the update was staged. topic: update-staged, status: " +
          update.state
      );
      Services.obs.notifyObservers(update, "update-staged", update.state);
    } finally {
      // This function being called is the one thing that tells us that staging
      // is done so be very sure that we don't exit it leaving the current
      // state at STATE_STAGING.
      // The only cases where we haven't already done a state transition are
      // error cases, so if another state isn't set, assume that we hit an error
      // and aborted the update.
      if (
        lazy.AUS.currentState == Ci.nsIApplicationUpdateService.STATE_STAGING
      ) {
        LOG("UpdateManager:refreshUpdateStatus - Setting state STATE_IDLE");
        transitionState(Ci.nsIApplicationUpdateService.STATE_IDLE);
      }
    }
  }

  /**
   * See nsIUpdateService.idl
   */
  async elevationOptedIn() {
    // The user has been been made aware that the update requires elevation.
    await lazy.AUS.init();
    let update = this._readyUpdate;
    if (!update) {
      return;
    }
    let status = readStatusFile(getReadyUpdateDir());
    let parts = status.split(":");
    update.state = parts[0];
    if (update.state == STATE_PENDING_ELEVATE) {
      LOG("UpdateManager:elevationOptedIn - Setting state to pending.");
      // Proceed with the pending update.
      // Note: STATE_PENDING_ELEVATE stands for "pending user's approval to
      // proceed with an elevated update". As long as we see this state, we will
      // notify the user of the availability of an update that requires
      // elevation. |elevationOptedIn| (this function) is called when the user
      // gives us approval to proceed, so we want to switch to STATE_PENDING.
      // The updater then detects whether or not elevation is required and
      // displays the elevation prompt if necessary. This last step does not
      // depend on the state in the status file.
      writeStatusFile(getReadyUpdateDir(), STATE_PENDING);
    } else {
      LOG("UpdateManager:elevationOptedIn - Not in pending-elevate state.");
    }
  }

  /**
   * See nsIUpdateService.idl
   */
  async cleanupDownloadingUpdate() {
    LOG(
      "UpdateManager:cleanupDownloadingUpdate - cleaning up downloading update."
    );
    await lazy.AUS.init();
    await cleanupDownloadingUpdate();
  }

  /**
   * See nsIUpdateService.idl
   */
  async cleanupReadyUpdate() {
    LOG("UpdateManager:cleanupReadyUpdate - cleaning up ready update.");
    await lazy.AUS.init();
    cleanupReadyUpdate();
  }

  /**
   * See nsIUpdateService.idl
   */
  async cleanupActiveUpdates() {
    LOG("UpdateManager:cleanupActiveUpdates - cleaning up active updates.");
    await lazy.AUS.init();
    await cleanupActiveUpdates();
  }

  /**
   * See nsIUpdateService.idl
   */
  async doInstallCleanup() {
    LOG("UpdateManager:doInstallCleanup - cleaning up");
    await lazy.AUS.init();

    let completionPromises = [];

    const delete_or_log = path =>
      IOUtils.remove(path).catch(ex =>
        console.error(`Failed to delete ${path}`, ex)
      );

    for (const key of [KEY_OLD_UPDROOT, KEY_UPDROOT]) {
      const root = Services.dirsvc.get(key, Ci.nsIFile);

      const activeUpdateXml = root.clone();
      activeUpdateXml.append(FILE_ACTIVE_UPDATE_XML);
      completionPromises.push(delete_or_log(activeUpdateXml.path));

      const downloadingMar = root.clone();
      downloadingMar.append(DIR_UPDATES);
      downloadingMar.append(DIR_UPDATE_DOWNLOADING);
      downloadingMar.append(FILE_UPDATE_MAR);
      completionPromises.push(delete_or_log(downloadingMar.path));

      const readyDir = root.clone();
      readyDir.append(DIR_UPDATES);
      readyDir.append(DIR_UPDATE_READY);
      const readyMar = readyDir.clone();
      readyMar.append(FILE_UPDATE_MAR);
      completionPromises.push(delete_or_log(readyMar.path));
      const readyStatus = readyDir.clone();
      readyStatus.append(FILE_UPDATE_STATUS);
      completionPromises.push(delete_or_log(readyStatus.path));
      const versionFile = readyDir.clone();
      versionFile.append(FILE_UPDATE_VERSION);
      completionPromises.push(delete_or_log(versionFile.path));
    }

    return Promise.allSettled(completionPromises);
  }

  /**
   * See nsIUpdateService.idl
   */
  async doUninstallCleanup() {
    LOG("UpdateManager:doUninstallCleanup - cleaning up.");
    await lazy.AUS.init();
    let completionPromises = [];

    completionPromises.push(
      IOUtils.remove(Services.dirsvc.get(KEY_UPDROOT, Ci.nsIFile).path, {
        recursive: true,
      }).catch(ex => console.error("Failed to remove update directory", ex))
    );
    completionPromises.push(
      IOUtils.remove(Services.dirsvc.get(KEY_OLD_UPDROOT, Ci.nsIFile).path, {
        recursive: true,
      }).catch(ex => console.error("Failed to remove old update directory", ex))
    );

    return Promise.allSettled(completionPromises);
  }

  classID = Components.ID("{093C2356-4843-4C65-8709-D7DBCBBE7DFB}");
  QueryInterface = ChromeUtils.generateQI([Ci.nsIUpdateManager]);
}

/**
 * CheckerService
 * Provides an interface for checking for new updates. When more checks are
 * made while an equivalent check is already in-progress, they will be coalesced
 * into a single update check request.
 */
export class CheckerService {
  #nextUpdateCheckId = 1;

  // Most of the update checking data is looked up via a "request key". This
  // allows us to lookup the request key for a particular check id, since
  // multiple checks can correspond to a single request.
  // When a check is cancelled or completed, it will be removed from this
  // object.
  #requestKeyByCheckId = {};

  // This object will relate request keys to update check data objects. The
  // format of the update check data objects is defined by
  // #makeUpdateCheckDataObject, below.
  // When an update request is cancelled (by all of the corresponding update
  // checks being cancelled) or completed, its key will be removed from this
  // object.
  #updateCheckData = {};

  constructor() {
    this.internal = {
      checkForUpdates: checkType => this.#checkForUpdates(checkType, true),
      QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateCheckerInternal]),
    };
  }

  #makeUpdateCheckDataObject(type, promise) {
    return { type, promise, request: null };
  }

  /**
   * Indicates whether the passed parameter is one of the valid enumerated
   * values that indicates a type of update check.
   */
  #validUpdateCheckType(checkType) {
    return [
      Ci.nsIUpdateChecker.BACKGROUND_CHECK,
      Ci.nsIUpdateChecker.FOREGROUND_CHECK,
    ].includes(checkType);
  }

  #getCanMigrate() {
    if (AppConstants.platform != "win") {
      return false;
    }

    // The first element of the array is whether the build target is 32 or 64
    // bit and the third element of the array is whether the client's Windows OS
    // system processor is 32 or 64 bit.
    let aryABI = lazy.UpdateUtils.ABI.split("-");
    if (aryABI[0] != "x86" || aryABI[2] != "x64") {
      return false;
    }

    let wrk = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );

    let regPath =
      "SOFTWARE\\Mozilla\\" + Services.appinfo.name + "\\32to64DidMigrate";
    let regValHKCU = lazy.WindowsRegistry.readRegKey(
      wrk.ROOT_KEY_CURRENT_USER,
      regPath,
      "Never",
      wrk.WOW64_32
    );
    let regValHKLM = lazy.WindowsRegistry.readRegKey(
      wrk.ROOT_KEY_LOCAL_MACHINE,
      regPath,
      "Never",
      wrk.WOW64_32
    );
    // The Never registry key value allows configuring a system to never migrate
    // any of the installations.
    if (regValHKCU === 1 || regValHKLM === 1) {
      LOG(
        "CheckerService:#getCanMigrate - all installations should not be " +
          "migrated"
      );
      return false;
    }

    let appBaseDirPath = getAppBaseDir().path;
    regValHKCU = lazy.WindowsRegistry.readRegKey(
      wrk.ROOT_KEY_CURRENT_USER,
      regPath,
      appBaseDirPath,
      wrk.WOW64_32
    );
    regValHKLM = lazy.WindowsRegistry.readRegKey(
      wrk.ROOT_KEY_LOCAL_MACHINE,
      regPath,
      appBaseDirPath,
      wrk.WOW64_32
    );
    // When the registry value is 1 for the installation directory path value
    // name then the installation has already been migrated once or the system
    // was configured to not migrate that installation.
    if (regValHKCU === 1 || regValHKLM === 1) {
      LOG(
        "CheckerService:#getCanMigrate - this installation should not be " +
          "migrated"
      );
      return false;
    }

    // When the registry value is 0 for the installation directory path value
    // name then the installation has updated to Firefox 56 and can be migrated.
    if (regValHKCU === 0 || regValHKLM === 0) {
      LOG("CheckerService:#getCanMigrate - this installation can be migrated");
      return true;
    }

    LOG(
      "CheckerService:#getCanMigrate - no registry entries for this " +
        "installation"
    );
    return false;
  }

  /**
   * See nsIUpdateService.idl
   */
  async getUpdateURL(checkType) {
    LOG("CheckerService:getUpdateURL - checkType: " + checkType);
    if (!this.#validUpdateCheckType(checkType)) {
      LOG("CheckerService:getUpdateURL - Invalid checkType");
      throw Components.Exception("", Cr.NS_ERROR_INVALID_ARG);
    }

    let url = Services.appinfo.updateURL;
    let updatePin;

    if (Services.policies) {
      let policies = Services.policies.getActivePolicies();
      if (policies) {
        if ("AppUpdateURL" in policies) {
          url = policies.AppUpdateURL.toString();
        }
        if ("AppUpdatePin" in policies) {
          updatePin = policies.AppUpdatePin;

          // Scalar ID: update.version_pin
          AUSTLMY.pingPinPolicy(updatePin);
        }
      }
    }

    if (!url) {
      LOG("CheckerService:getUpdateURL - update URL not defined");
      return null;
    }

    url = await lazy.UpdateUtils.formatUpdateURL(url);

    if (checkType == Ci.nsIUpdateChecker.FOREGROUND_CHECK) {
      url += (url.includes("?") ? "&" : "?") + "force=1";
    }

    if (this.#getCanMigrate()) {
      url += (url.includes("?") ? "&" : "?") + "mig64=1";
    }

    if (updatePin) {
      url +=
        (url.includes("?") ? "&" : "?") +
        "pin=" +
        encodeURIComponent(updatePin);
    }

    LOG("CheckerService:getUpdateURL - update URL: " + url);
    return url;
  }

  /**
   * See nsIUpdateService.idl
   */
  checkForUpdates(checkType) {
    return this.#checkForUpdates(checkType, false);
  }

  #checkForUpdates(checkType, internal) {
    // Note that we should run update initialization if it was invoked
    // externally (i.e. `internal == false`). But initialization is async and
    // the asynchronous work of this function happens in `this.#updateCheck`, so
    // we will delay initialization until then.

    LOG("CheckerService:checkForUpdates - checkType: " + checkType);
    if (!this.#validUpdateCheckType(checkType)) {
      LOG("CheckerService:checkForUpdates - Invalid checkType");
      throw Components.Exception("", Cr.NS_ERROR_INVALID_ARG);
    }

    let checkId = this.#nextUpdateCheckId;
    this.#nextUpdateCheckId += 1;

    // `checkType == FOREGROUND_CHECK` can override `canCheckForUpdates`. But
    // nothing should override enterprise policies.
    if (lazy.AUS.disabled) {
      LOG("CheckerService:checkForUpdates - disabled by policy");
      return this.#getChecksNotAllowedObject(checkId);
    }
    if (
      checkType == Ci.nsIUpdateChecker.BACKGROUND_CHECK &&
      !lazy.AUS.canCheckForUpdates
    ) {
      LOG("CheckerService:checkForUpdates - !canCheckForUpdates");
      return this.#getChecksNotAllowedObject(checkId);
    }

    // We want to combine simultaneous requests, but only ones that are
    // equivalent. If, say, one of them uses the force parameter and one
    // doesn't, we want those two requests to remain separate. This key will
    // allow us to map equivalent requests together. It is also the key that we
    // use to lookup the update check data in this.#updateCheckData.
    let requestKey = checkType;

    if (requestKey in this.#updateCheckData) {
      LOG(
        `CheckerService:checkForUpdates - Connecting check id ${checkId} to ` +
          `existing check request.`
      );
    } else {
      LOG(
        `CheckerService:checkForUpdates - Making new check request for check ` +
          `id ${checkId}.`
      );
      this.#updateCheckData[requestKey] = this.#makeUpdateCheckDataObject(
        checkType,
        this.#updateCheck(checkType, requestKey, internal)
      );
    }

    this.#requestKeyByCheckId[checkId] = requestKey;

    return {
      id: checkId,
      result: this.#updateCheckData[requestKey].promise,
      QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateCheck]),
    };
  }

  #getChecksNotAllowedObject(checkId) {
    return {
      id: checkId,
      result: Promise.resolve(
        Object.freeze({
          checksAllowed: false,
          succeeded: false,
          request: null,
          updates: [],
          QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateCheckResult]),
        })
      ),
      QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateCheck]),
    };
  }

  async #updateCheck(checkType, requestKey, internal) {
    if (!internal) {
      await lazy.AUS.init();
    }

    await waitForOtherInstances();

    let url;
    try {
      url = await this.getUpdateURL(checkType);
    } catch (ex) {}

    if (!url) {
      LOG("CheckerService:#updateCheck - !url");
      return this.#getCheckFailedObject("update_url_not_available");
    }

    let request = new XMLHttpRequest();
    request.open("GET", url, true);
    // Prevent the request from reading from the cache.
    request.channel.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE;
    // Prevent the request from writing to the cache.
    request.channel.loadFlags |= Ci.nsIRequest.INHIBIT_CACHING;
    // Disable cutting edge features, like TLS 1.3, where middleboxes might
    // brick us
    request.channel.QueryInterface(Ci.nsIHttpChannelInternal).beConservative =
      true;

    request.overrideMimeType("text/xml");
    // The Cache-Control header is only interpreted by proxies and the
    // final destination. It does not help if a resource is already
    // cached locally.
    request.setRequestHeader("Cache-Control", "no-cache");
    // HTTP/1.0 servers might not implement Cache-Control and
    // might only implement Pragma: no-cache
    request.setRequestHeader("Pragma", "no-cache");

    const UPDATE_CHECK_LOAD_SUCCESS = 1;
    const UPDATE_CHECK_LOAD_ERROR = 2;
    const UPDATE_CHECK_CANCELLED = 3;

    let result = await new Promise(resolve => {
      // It's important that nothing potentially asynchronous happens between
      // checking if the request has been cancelled and starting the request.
      // If an update check cancellation happens before dispatching the request
      // and we end up dispatching it anyways, we will never call cancel on the
      // request later and the cancellation effectively won't happen.
      if (!(requestKey in this.#updateCheckData)) {
        LOG(
          "CheckerService:#updateCheck - check was cancelled before request " +
            "was able to start"
        );
        resolve(UPDATE_CHECK_CANCELLED);
        return;
      }

      let onLoad = _event => {
        request.removeEventListener("load", onLoad);
        LOG("CheckerService:#updateCheck - request got 'load' event");
        resolve(UPDATE_CHECK_LOAD_SUCCESS);
      };
      request.addEventListener("load", onLoad);
      let onError = _event => {
        request.removeEventListener("error", onLoad);
        LOG("CheckerService:#updateCheck - request got 'error' event");
        resolve(UPDATE_CHECK_LOAD_ERROR);
      };
      request.addEventListener("error", onError);

      LOG("CheckerService:#updateCheck - sending request to: " + url);
      request.send(null);
      this.#updateCheckData[requestKey].request = request;
    });

    // Remove all entries for this request key. This marks the request and the
    // associated check ids as no longer in-progress.
    delete this.#updateCheckData[requestKey];
    for (const checkId of Object.keys(this.#requestKeyByCheckId)) {
      if (this.#requestKeyByCheckId[checkId] == requestKey) {
        delete this.#requestKeyByCheckId[checkId];
      }
    }

    if (result == UPDATE_CHECK_CANCELLED) {
      return this.#getCheckFailedObject(Cr.NS_BINDING_ABORTED);
    }

    if (result == UPDATE_CHECK_LOAD_ERROR) {
      let status = this.#getChannelStatus(request);
      LOG("CheckerService:#updateCheck - Failed. request.status: " + status);

      // Set MitM pref.
      try {
        let secInfo = request.channel.securityInfo;
        if (
          secInfo.errorCodeString == "SEC_ERROR_UNKNOWN_ISSUER" &&
          secInfo.serverCert &&
          secInfo.serverCert.issuerName
        ) {
          Services.prefs.setStringPref(
            "security.pki.mitm_canary_issuer",
            secInfo.serverCert.issuerName
          );
        }
      } catch (e) {
        LOG("CheckerService:#updateCheck - Getting secInfo failed.");
      }

      return this.#getCheckFailedObject(status, 404, request);
    }

    LOG("CheckerService:#updateCheck - request completed downloading document");
    Services.prefs.clearUserPref("security.pki.mitm_canary_issuer");
    // Check whether there is a mitm, i.e. check whether the root cert is
    // built-in or not.
    try {
      let sslStatus = request.channel.securityInfo;
      if (sslStatus) {
        Services.prefs.setBoolPref(
          "security.pki.mitm_detected",
          !sslStatus.isBuiltCertChainRootBuiltInRoot
        );
      }
    } catch (e) {
      LOG("CheckerService:#updateCheck - Getting sslStatus failed.");
    }

    let updates;
    try {
      // Analyze the resulting DOM and determine the set of updates.
      updates = this.#parseUpdates(request);
    } catch (e) {
      LOG(
        "CheckerService:#updateCheck - there was a problem checking for " +
          "updates. Exception: " +
          e
      );
      let status = this.#getChannelStatus(request);
      // If we can't find an error string specific to this status code,
      // just use the 200 message from above, which means everything
      // "looks" fine but there was probably an XML error or a bogus file.
      return this.#getCheckFailedObject(status, 200, request);
    }

    LOG(
      "CheckerService:#updateCheck - number of updates available: " +
        updates.length
    );

    if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_BACKGROUNDERRORS)) {
      Services.prefs.clearUserPref(PREF_APP_UPDATE_BACKGROUNDERRORS);
    }

    return Object.freeze({
      checksAllowed: true,
      succeeded: true,
      request,
      updates,
      QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateCheckResult]),
    });
  }

  /**
   * @param   errorCode
   *          The error code to include in the return value. If possible, we
   *          will get the update status text based on this error code.
   * @param   defaultCode
   *          Optional. The error code to use to get the status text if there
   *          isn't status text available for `errorCode`.
   * @param   request
   *          The XMLHttpRequest used to check for updates. Or null, if one was
   *          never constructed.
   * @returns An nsIUpdateCheckResult object indicating an error, using the
   *          error data passed to this function.
   */
  #getCheckFailedObject(
    errorCode,
    defaultCode = Cr.NS_BINDING_FAILED,
    request = null
  ) {
    let update = new Update(null);
    update.errorCode = errorCode;
    update.statusText = getStatusTextFromCode(errorCode, defaultCode);

    if (errorCode == Cr.NS_ERROR_OFFLINE) {
      // We use a separate constant here because nsIUpdate.errorCode is signed
      update.errorCode = NETWORK_ERROR_OFFLINE;
    } else if (this.#isHttpStatusCode(errorCode)) {
      update.errorCode = HTTP_ERROR_OFFSET + errorCode;
    }

    return Object.freeze({
      checksAllowed: true,
      succeeded: false,
      request,
      updates: [update],
      QueryInterface: ChromeUtils.generateQI([Ci.nsIUpdateCheckResult]),
    });
  }

  /**
   * Returns the status code for the XMLHttpRequest
   */
  #getChannelStatus(request) {
    var status = 0;
    try {
      status = request.status;
    } catch (e) {}

    if (status == 0) {
      status = request.channel.QueryInterface(Ci.nsIRequest).status;
    }
    return status;
  }

  #isHttpStatusCode(status) {
    return status >= 100 && status <= 599;
  }

  /**
   * @param   request
   *          The XMLHttpRequest that successfully loaded the update XML.
   * @returns An array of 0 or more nsIUpdate objects describing the available
   *          updates.
   * @throws  If the XML document element node name is not updates.
   */
  #parseUpdates(request) {
    let updatesElement = request.responseXML.documentElement;
    if (!updatesElement) {
      LOG("CheckerService:#parseUpdates - empty updates document?!");
      return [];
    }

    if (updatesElement.nodeName != "updates") {
      LOG("CheckerService:#parseUpdates - unexpected node name!");
      throw new Error(
        "Unexpected node name, expected: updates, got: " +
          updatesElement.nodeName
      );
    }

    let updates = [];
    for (const updateElement of updatesElement.childNodes) {
      if (
        updateElement.nodeType != updateElement.ELEMENT_NODE ||
        updateElement.localName != "update"
      ) {
        continue;
      }

      let update;
      try {
        update = new Update(updateElement);
      } catch (e) {
        LOG("CheckerService:#parseUpdates - invalid <update/>, ignoring...");
        continue;
      }
      update.serviceURL = request.responseURL;
      update.channel = lazy.UpdateUtils.UpdateChannel;
      updates.push(update);
    }

    return updates;
  }

  /**
   * See nsIUpdateService.idl
   */
  stopCheck(checkId) {
    if (!(checkId in this.#requestKeyByCheckId)) {
      LOG(`CheckerService:stopCheck - Non-existent check id ${checkId}`);
      return;
    }
    LOG(`CheckerService:stopCheck - Cancelling check id ${checkId}`);
    let requestKey = this.#requestKeyByCheckId[checkId];
    delete this.#requestKeyByCheckId[checkId];
    if (Object.values(this.#requestKeyByCheckId).includes(requestKey)) {
      LOG(
        `CheckerService:stopCheck - Not actually cancelling request because ` +
          `other check id's depend on it.`
      );
    } else {
      LOG(
        `CheckerService:stopCheck - This is the last check using this ` +
          `request. Cancelling the request now.`
      );
      let request = this.#updateCheckData[requestKey].request;
      delete this.#updateCheckData[requestKey];
      if (request) {
        LOG(`CheckerService:stopCheck - Aborting XMLHttpRequest`);
        request.abort();
      } else {
        LOG(
          `CheckerService:stopCheck - Not aborting XMLHttpRequest. It ` +
            `doesn't appear to have started yet.`
        );
      }
    }
  }

  /**
   * See nsIUpdateService.idl
   */
  stopAllChecks() {
    LOG("CheckerService:stopAllChecks - stopping all checks.");
    for (const checkId of Object.keys(this.#requestKeyByCheckId)) {
      this.stopCheck(checkId);
    }
  }

  classID = Components.ID("{898CDC9B-E43F-422F-9CC4-2F6291B415A3}");
  QueryInterface = ChromeUtils.generateQI([Ci.nsIUpdateChecker]);
}

class Downloader {
  /**
   * The nsIUpdatePatch that we are downloading
   */
  _patch = null;

  /**
   * The nsIUpdate that we are downloading
   */
  _update = null;

  /**
   * The nsIRequest object handling the download.
   */
  _request = null;

  /**
   * Whether or not the update being downloaded is a complete replacement of
   * the user's existing installation or a patch representing the difference
   * between the new version and the previous version.
   */
  isCompleteUpdate = null;

  /**
   * We get the nsIRequest from nsIBITS asynchronously. When downloadUpdate has
   * been called, but this._request is not yet valid, _pendingRequest will be
   * a promise that will resolve when this._request has been set.
   */
  _pendingRequest = null;

  /**
   * When using BITS, cancel actions happen asynchronously. This variable
   * keeps track of any cancel action that is in-progress.
   * If the cancel action fails, this will be set back to null so that the
   * action can be attempted again. But if the cancel action succeeds, the
   * resolved promise will remain stored in this variable to prevent cancel
   * from being called twice (which, for BITS, is an error).
   */
  _cancelPromise = null;

  /**
   * BITS receives progress notifications slowly, unless a user is watching.
   * This tracks what frequency notifications are happening at.
   *
   * This is needed because BITS downloads are started asynchronously.
   * Specifically, this is needed to prevent a situation where the download is
   * still starting (Downloader._pendingRequest has not resolved) when the first
   * observer registers itself. Without this variable, there is no way of
   * knowing whether the download was started as Active or Idle and, therefore,
   * we don't know if we need to start Active mode when _pendingRequest
   * resolves.
   */
  _bitsActiveNotifications = false;

  /**
   * This is a function that when called will stop the update process from
   * waiting for language pack updates. This is for safety to ensure that a
   * problem in the add-ons manager doesn't delay updates by much.
   */
  _langPackTimeout = null;

  /**
   * If gOnlyDownloadUpdatesThisSession is true, we prevent the update process
   * from progressing past the downloading stage. If the download finishes,
   * pretend that it hasn't in order to keep the current update in the
   * "downloading" state.
   */
  _pretendingDownloadIsNotDone = false;

  /**
   * Manages the download of updates
   * @param   background
   *          Whether or not this downloader is operating in background
   *          update mode.
   * @param   updateService
   *          The update service that created this downloader.
   * @constructor
   */
  constructor(updateService) {
    LOG("Creating Downloader");
    this.updateService = updateService;
  }

  /**
   * Cancels the active download.
   *
   * For a BITS download, this will cancel and remove the download job. For
   * an nsIIncrementalDownload, this will stop the download, but leaves the
   * data around to allow the transfer to be resumed later.
   */
  async cancel(cancelError) {
    LOG("Downloader: cancel");
    if (cancelError === undefined) {
      cancelError = Cr.NS_BINDING_ABORTED;
    }
    if (this.usingBits) {
      // If a cancel action is already in progress, just return when that
      // promise resolved. Trying to cancel the same request twice is an error.
      if (this._cancelPromise) {
        await this._cancelPromise;
        return;
      }

      if (this._pendingRequest) {
        await this._pendingRequest;
      }
      if (this._patch.getProperty("bitsId") != null) {
        // Make sure that we don't try to resume this download after it was
        // cancelled.
        this._patch.deleteProperty("bitsId");
      }
      try {
        this._cancelPromise = this._request.cancelAsync(cancelError);
        await this._cancelPromise;
      } catch (e) {
        // On success, we will not set the cancel promise to null because
        // we want to prevent two cancellations of the same request. But
        // retrying after a failed cancel is not an error, so we will set the
        // cancel promise to null in the failure case.
        this._cancelPromise = null;
        throw e;
      }
    } else if (this._request && this._request instanceof Ci.nsIRequest) {
      // Normally, cancelling an nsIIncrementalDownload results in it stopping
      // the download but leaving the downloaded data so that we can resume the
      // download later. If we've already finished the download, there is no
      // transfer to stop.
      // Note that this differs from the BITS case. Cancelling a BITS job, even
      // when the transfer has completed, results in all data being deleted.
      // Therefore, even if the transfer has completed, cancelling a BITS job
      // has effects that we must not skip.
      if (this._pretendingDownloadIsNotDone) {
        LOG(
          "Downloader: cancel - Ignoring cancel request of finished download"
        );
      } else {
        this._request.cancel(cancelError);
      }
    }
  }

  /**
   * Verify the downloaded file.  We assume that the download is complete at
   * this point.
   */
  _verifyDownload() {
    LOG("Downloader:_verifyDownload called");
    if (!this._request) {
      AUSTLMY.pingDownloadCode(
        this.isCompleteUpdate,
        AUSTLMY.DWNLD_ERR_VERIFY_NO_REQUEST
      );
      return false;
    }

    let destination = getDownloadingUpdateDir();
    destination.append(FILE_UPDATE_MAR);

    // Ensure that the file size matches the expected file size.
    if (destination.fileSize != this._patch.size) {
      LOG("Downloader:_verifyDownload downloaded size != expected size.");
      AUSTLMY.pingDownloadCode(
        this.isCompleteUpdate,
        AUSTLMY.DWNLD_ERR_VERIFY_PATCH_SIZE_NOT_EQUAL
      );
      return false;
    }

    LOG("Downloader:_verifyDownload downloaded size == expected size.");
    return true;
  }

  /**
   * Select the patch to use given the current state of updateDir and the given
   * set of update patches.
   * @param   update
   *          A nsIUpdate object to select a patch from
   * @return  A nsIUpdatePatch object to download
   */
  async _selectPatch(update) {
    // Given an update to download, we will always try to download the patch
    // for a partial update over the patch for a full update.

    // Look to see if any of the patches in the Update object has been
    // pre-selected for download, otherwise we must figure out which one
    // to select ourselves.
    var selectedPatch = update.selectedPatch;

    var state = selectedPatch ? selectedPatch.state : STATE_NONE;

    // If this is a patch that we know about, then select it.  If it is a patch
    // that we do not know about, then remove it and use our default logic.
    var useComplete = false;
    if (selectedPatch) {
      LOG(
        "Downloader:_selectPatch - found existing patch with state: " + state
      );
      if (state == STATE_DOWNLOADING) {
        LOG("Downloader:_selectPatch - resuming download");
        return selectedPatch;
      }
      if (
        state == STATE_PENDING ||
        state == STATE_PENDING_SERVICE ||
        state == STATE_PENDING_ELEVATE ||
        state == STATE_APPLIED ||
        state == STATE_APPLIED_SERVICE
      ) {
        LOG("Downloader:_selectPatch - already downloaded");
        return null;
      }

      // When downloading the patch failed using BITS, there hasn't been an
      // attempt to download the patch using the internal application download
      // mechanism, and an attempt to stage or apply the patch hasn't failed
      // which indicates that a different patch should be downloaded since
      // re-downloading the same patch with the internal application download
      // mechanism will likely also fail when trying to stage or apply it then
      // try to download the same patch using the internal application download
      // mechanism.
      selectedPatch.QueryInterface(Ci.nsIWritablePropertyBag);
      if (
        selectedPatch.getProperty("bitsResult") != null &&
        selectedPatch.getProperty("internalResult") == null &&
        !selectedPatch.errorCode
      ) {
        LOG(
          "Downloader:_selectPatch - Falling back to non-BITS download " +
            "mechanism for the same patch due to existing BITS result: " +
            selectedPatch.getProperty("bitsResult")
        );
        return selectedPatch;
      }

      if (update && selectedPatch.type == "complete") {
        // This is a pretty fatal error.  Just bail.
        LOG("Downloader:_selectPatch - failed to apply complete patch!");
        await cleanupDownloadingUpdate();
        return null;
      }

      // Something went wrong when we tried to apply the previous patch.
      // Try the complete patch next time.
      useComplete = true;
      selectedPatch = null;
    }

    // If we were not able to discover an update from a previous download, we
    // select the best patch from the given set.
    var partialPatch = getPatchOfType(update, "partial");
    if (!useComplete) {
      selectedPatch = partialPatch;
    }
    if (!selectedPatch) {
      if (lazy.UM.internal.readyUpdate) {
        // If we already have a ready update, we download partials only.
        LOG(
          "Downloader:_selectPatch - not selecting a complete patch because " +
            "this is not the first download of the session"
        );
        return null;
      }

      if (partialPatch) {
        partialPatch.selected = false;
      }
      selectedPatch = getPatchOfType(update, "complete");
    }

    // if update only contains a partial patch, selectedPatch == null here if
    // the partial patch has been attempted and fails and we're trying to get a
    // complete patch
    if (selectedPatch) {
      selectedPatch.selected = true;
      update.isCompleteUpdate = selectedPatch.type == "complete";
    }

    LOG(
      "Downloader:_selectPatch - Patch selected. Assigning update to " +
        "downloadingUpdate."
    );
    lazy.UM.internal.downloadingUpdate = update;

    return selectedPatch;
  }

  /**
   * Whether or not the user wants to be notified that an update is being
   * downloaded.
   */
  get _notifyDuringDownload() {
    return Services.prefs.getBoolPref(
      PREF_APP_UPDATE_NOTIFYDURINGDOWNLOAD,
      false
    );
  }

  _notifyDownloadStatusObservers() {
    if (this._notifyDuringDownload) {
      let status = this.updateService.isDownloading ? "downloading" : "idle";
      Services.obs.notifyObservers(this._update, "update-downloading", status);
    }
  }

  /**
   * Whether or not we are currently downloading something.
   */
  get isBusy() {
    return this._request != null || this._pendingRequest != null;
  }

  get usingBits() {
    return this._pendingRequest != null || this._request instanceof BitsRequest;
  }

  /**
   * Returns true if the specified patch can be downloaded with BITS.
   */
  _canUseBits(patch) {
    if (getCanUseBits() != "CanUseBits") {
      // This will have printed its own logging. No need to print more.
      return false;
    }
    // Regardless of success or failure, don't download the same patch with BITS
    // twice.
    if (patch.getProperty("bitsResult") != null) {
      LOG(
        "Downloader:_canUseBits - Not using BITS because it was already tried"
      );
      return false;
    }
    LOG("Downloader:_canUseBits - Patch is able to use BITS download");
    return true;
  }

  /**
   * Instruct the add-ons manager to start downloading language pack updates in
   * preparation for the current update.
   */
  _startLangPackUpdates() {
    if (!Services.prefs.getBoolPref(PREF_APP_UPDATE_LANGPACK_ENABLED, false)) {
      return;
    }

    // A promise that we can resolve at some point to time out the language pack
    // update process.
    let timeoutPromise = new Promise(resolve => {
      this._langPackTimeout = resolve;
    });

    let update = unwrap(this._update);

    let existing = LangPackUpdates.get(update);
    if (existing) {
      // We have already started staging lang packs for this update, no need to
      // do it again.
      return;
    }

    // Note that we don't care about success or failure here, either way we will
    // continue with the update process.
    let langPackPromise = lazy.AddonManager.stageLangpacksForAppUpdate(
      update.appVersion,
      update.appVersion
    )
      .catch(error => {
        LOG(
          `Add-ons manager threw exception while updating language packs: ${error}`
        );
      })
      .finally(() => {
        let timerId = this._langPackTimeout.gleanTimerId;
        this._langPackTimeout = null;
        if (timerId) {
          Glean.update.langpackOvertime.stopAndAccumulate(timerId);
        }
      });

    LangPackUpdates.set(
      update,
      Promise.race([langPackPromise, timeoutPromise])
    );
  }

  /**
   * Given a patch URL, return a URL possibly modified with extra query
   * parameters and extra headers.  The extras help identify whether this update
   * is driven by a regular browsing Firefox or by a background update task.
   *
   * @param {string} [patchURL] Unmodified patch URL.
   * @return { url, extraHeaders }
   */
  _maybeWithExtras(patchURL) {
    let shouldAddExtras = true;
    if (AppConstants.MOZ_APP_NAME !== "firefox") {
      shouldAddExtras = false;
    }
    if (Services.policies) {
      let policies = Services.policies.getActivePolicies();
      if (policies) {
        if ("AppUpdateURL" in policies) {
          shouldAddExtras = false;
        }
      }
    }

    if (!shouldAddExtras) {
      LOG("Downloader:_maybeWithExtras - Not adding extras");
      return { url: patchURL, extraHeaders: "\r\n" };
    }

    LOG("Downloader:_maybeWithExtras - Adding extras");

    let modeStr = lazy.gIsBackgroundTaskMode ? "1" : "0";
    let extraHeaders = `X-BackgroundTaskMode: ${modeStr}\r\n`;
    let extraParameters = [["backgroundTaskMode", modeStr]];

    if (lazy.gIsBackgroundTaskMode) {
      const bts = Cc["@mozilla.org/backgroundtasks;1"].getService(
        Ci.nsIBackgroundTasks
      );
      extraHeaders += `X-BackgroundTaskName: ${bts.backgroundTaskName()}\r\n`;
      extraParameters.push(["backgroundTaskName", bts.backgroundTaskName()]);
    }

    extraHeaders += "\r\n";

    let url = patchURL;
    let parsedUrl = URL.parse(url);
    if (parsedUrl) {
      for (let [p, v] of extraParameters) {
        parsedUrl.searchParams.set(p, v);
      }
      url = parsedUrl.href;
    } else {
      LOG("Downloader:_maybeWithExtras - Failed to parse patch URL!");
    }

    return { url, extraHeaders };
  }

  /**
   * Download and stage the given update.
   * @param   update
   *          A nsIUpdate object to download a patch for. Cannot be null.
   */
  async downloadUpdate(update) {
    LOG("Downloader:downloadUpdate");
    if (!update) {
      AUSTLMY.pingDownloadCode(undefined, AUSTLMY.DWNLD_ERR_NO_UPDATE);
      throw Components.Exception("", Cr.NS_ERROR_NULL_POINTER);
    }

    var updateDir = getDownloadingUpdateDir();

    this._update = update;

    // This function may return null, which indicates that there are no patches
    // to download.
    this._patch = await this._selectPatch(update);
    if (!this._patch) {
      LOG("Downloader:downloadUpdate - no patch to download");
      AUSTLMY.pingDownloadCode(undefined, AUSTLMY.DWNLD_ERR_NO_UPDATE_PATCH);
      return Ci.nsIApplicationUpdateService.DOWNLOAD_FAILURE_GENERIC;
    }
    // The update and the patch implement nsIWritablePropertyBag. Expose that
    // interface immediately after a patch is assigned so that
    // this.(_patch|_update).(get|set)Property can always safely be called.
    this._update.QueryInterface(Ci.nsIWritablePropertyBag);
    this._patch.QueryInterface(Ci.nsIWritablePropertyBag);

    if (
      this._update.getProperty("disableBackgroundUpdates") != null &&
      lazy.gIsBackgroundTaskMode
    ) {
      LOG(
        "Downloader:downloadUpdate - Background update disabled by update " +
          "advertisement"
      );
      return Ci.nsIApplicationUpdateService.DOWNLOAD_FAILURE_GENERIC;
    }

    this.isCompleteUpdate = this._patch.type == "complete";

    let canUseBits = false;
    // Allow the advertised update to disable BITS.
    if (this._update.getProperty("disableBITS") != null) {
      LOG(
        "Downloader:downloadUpdate - BITS downloads disabled by update " +
          "advertisement"
      );
    } else {
      canUseBits = this._canUseBits(this._patch);
    }

    // When using Firefox and Mozilla's update server, add extra headers and
    // extra query parameters identifying whether this request is on behalf of a
    // regular browsing profile (0) or a background task (1).  This helps
    // understand bandwidth usage of background updates in production.
    let { url, extraHeaders } = this._maybeWithExtras(this._patch.URL);

    if (!canUseBits) {
      this._pendingRequest = null;

      let patchFile = updateDir.clone();
      patchFile.append(FILE_UPDATE_MAR);

      // Background updates generally should not fall back to internal (Necko)
      // downloads: on Windows, they should only use Windows BITS.  In
      // automation, this pref allows Necko for testing.
      let allowDownloadsWithoutBITS =
        Cu.isInAutomation &&
        Services.prefs.getBoolPref(
          PREF_APP_UPDATE_BACKGROUND_ALLOWDOWNLOADSWITHOUTBITS,
          false
        );
      if (lazy.gIsBackgroundTaskMode && !allowDownloadsWithoutBITS) {
        // We don't normally run a background update if we can't use BITS, but
        // this branch is possible because we do fall back from BITS failures by
        // attempting an internal download.
        // If this happens, we are just going to need to wait for interactive
        // Firefox to download the update. We don't, however, want to be in the
        // "downloading" state when interactive Firefox runs because we want to
        // download the newest update available which, at that point, may not be
        // the one that we are currently trying to download.
        // However, we can't just unconditionally clobber the current update
        // because interactive Firefox might already be part way through an
        // internal update download, and we definitely don't want to interrupt
        // that.
        let readyUpdateDir = getReadyUpdateDir();
        let status = readStatusFile(readyUpdateDir);
        // nsIIncrementalDownload doesn't use an intermediate download location
        // for partially downloaded files. If we have started an update
        // download with it, it will be available at its ultimate location.
        if (!(status == STATE_DOWNLOADING && patchFile.exists())) {
          LOG(
            "Downloader:downloadUpdate - Can't download with internal " +
              "downloader from a background task. Cleaning up downloading " +
              "update."
          );
          await cleanupDownloadingUpdate();
        }
        return Ci.nsIApplicationUpdateService
          .DOWNLOAD_FAILURE_CANNOT_RESUME_IN_BACKGROUND;
      }

      // The interval is 0 since there is no need to throttle downloads.
      let interval = 0;

      LOG(
        "Downloader:downloadUpdate - Starting nsIIncrementalDownload with " +
          "url: " +
          url +
          ", path: " +
          patchFile.path +
          ", interval: " +
          interval
      );
      let uri = Services.io.newURI(url);

      this._request = Cc[
        "@mozilla.org/network/incremental-download;1"
      ].createInstance(Ci.nsIIncrementalDownload);

      this._request.init(
        uri,
        patchFile,
        DOWNLOAD_CHUNK_SIZE,
        interval,
        extraHeaders
      );
      this._request.start(this, null);
    } else {
      this._bitsActiveNotifications = this.hasDownloadListeners;
      this._cancelPromise = null;

      this._pendingRequest = this.updateService.makeBitsRequest({
        activeListeners: this.hasDownloadListeners,
        bitsId: this._patch.getProperty("bitsId"),
        observer: this,
        url,
        extraHeaders,
      });

      let request;
      try {
        request = await this._pendingRequest;
      } catch (error) {
        if (
          (error.type == Ci.nsIBits.ERROR_TYPE_FAILED_TO_GET_BITS_JOB ||
            error.type == Ci.nsIBits.ERROR_TYPE_FAILED_TO_CONNECT_TO_BCM) &&
          error.action == Ci.nsIBits.ERROR_ACTION_MONITOR_DOWNLOAD &&
          error.stage == Ci.nsIBits.ERROR_STAGE_BITS_CLIENT &&
          error.codeType == Ci.nsIBits.ERROR_CODE_TYPE_HRESULT &&
          error.code == HRESULT_E_ACCESSDENIED
        ) {
          LOG(
            "Downloader:downloadUpdate - Failed to connect to existing " +
              "BITS job. It is likely owned by another user."
          );
          // This isn't really a failure code since the BITS job may be working
          // just fine on another account, so convert this to a code that
          // indicates that. This will make it easier to identify in telemetry.
          error.type = Ci.nsIBits.ERROR_TYPE_ACCESS_DENIED_EXPECTED;
          error.codeType = Ci.nsIBits.ERROR_CODE_TYPE_NONE;
          error.code = null;
          // When we detect this situation, disable BITS until Firefox shuts
          // down. There are a couple of reasons for this. First, without any
          // kind of flag, we enter an infinite loop here where we keep trying
          // BITS over and over again (normally setting bitsResult prevents
          // this, but we don't know the result of the BITS job, so we don't
          // want to set that). Second, since we are trying to update, this
          // process must have the update mutex. We don't ever give up the
          // update mutex, so even if the other user starts Firefox, they will
          // not complete the BITS job while this Firefox instance is around.
          gBITSInUseByAnotherUser = true;
        } else {
          this._patch.setProperty("bitsResult", Cr.NS_ERROR_FAILURE);
          lazy.UM.saveUpdates();

          LOG(
            "Downloader:downloadUpdate - Failed to start to BITS job. " +
              "Error: " +
              error
          );
        }

        this._pendingRequest = null;

        AUSTLMY.pingBitsError(this.isCompleteUpdate, error);

        // Try download again with nsIIncrementalDownload
        return this.downloadUpdate(this._update);
      }

      this._request = request;
      this._patch.setProperty("bitsId", request.bitsId);

      LOG(
        "Downloader:downloadUpdate - BITS download running. BITS ID: " +
          request.bitsId
      );

      if (this.hasDownloadListeners) {
        this._maybeStartActiveNotifications();
      } else {
        this._maybeStopActiveNotifications();
      }

      lazy.UM.saveUpdates();
      this._pendingRequest = null;
    }

    if (!lazy.UM.internal.readyUpdate) {
      LOG("Downloader:downloadUpdate - Setting status to downloading");
      writeStatusFile(getReadyUpdateDir(), STATE_DOWNLOADING);
    }
    if (this._patch.state != STATE_DOWNLOADING) {
      LOG("Downloader:downloadUpdate - Setting state to downloading");
      this._patch.state = STATE_DOWNLOADING;
      lazy.UM.saveUpdates();
    }

    // If we are downloading a second update, we don't change the state until
    // STATE_SWAP.
    if (lazy.AUS.currentState == Ci.nsIApplicationUpdateService.STATE_PENDING) {
      LOG(
        "Downloader:downloadUpdate - not setting state because download is " +
          "already pending."
      );
    } else {
      LOG(
        "Downloader:downloadUpdate - setting currentState to STATE_DOWNLOADING"
      );
      transitionState(Ci.nsIApplicationUpdateService.STATE_DOWNLOADING);
    }

    this._startLangPackUpdates();

    this._notifyDownloadStatusObservers();

    return Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS;
  }

  /**
   * This is run when a download listener is added.
   */
  onDownloadListenerAdded() {
    // Increase the status update frequency when someone starts listening
    this._maybeStartActiveNotifications();
  }

  /**
   * This is run when a download listener is removed.
   */
  onDownloadListenerRemoved() {
    // Decrease the status update frequency when no one is listening
    if (!this.hasDownloadListeners) {
      this._maybeStopActiveNotifications();
    }
  }

  get hasDownloadListeners() {
    return this.updateService.hasDownloadListeners;
  }

  /**
   * This speeds up BITS progress notifications in response to a user watching
   * the notifications.
   */
  async _maybeStartActiveNotifications() {
    if (
      this.usingBits &&
      !this._bitsActiveNotifications &&
      this.hasDownloadListeners &&
      this._request
    ) {
      LOG(
        "Downloader:_maybeStartActiveNotifications - Starting active " +
          "notifications"
      );
      this._bitsActiveNotifications = true;
      await Promise.all([
        this._request
          .setNoProgressTimeout(BITS_ACTIVE_NO_PROGRESS_TIMEOUT_SECS)
          .catch(error => {
            LOG(
              "Downloader:_maybeStartActiveNotifications - Failed to set " +
                "no progress timeout. Error: " +
                error
            );
          }),
        this._request
          .changeMonitorInterval(BITS_ACTIVE_POLL_RATE_MS)
          .catch(error => {
            LOG(
              "Downloader:_maybeStartActiveNotifications - Failed to increase " +
                "status update frequency. Error: " +
                error
            );
          }),
      ]);
    }
  }

  /**
   * This slows down BITS progress notifications in response to a user no longer
   * watching the notifications.
   */
  async _maybeStopActiveNotifications() {
    if (
      this.usingBits &&
      this._bitsActiveNotifications &&
      !this.hasDownloadListeners &&
      this._request
    ) {
      LOG(
        "Downloader:_maybeStopActiveNotifications - Stopping active " +
          "notifications"
      );
      this._bitsActiveNotifications = false;
      await Promise.all([
        this._request
          .setNoProgressTimeout(BITS_IDLE_NO_PROGRESS_TIMEOUT_SECS)
          .catch(error => {
            LOG(
              "Downloader:_maybeStopActiveNotifications - Failed to set " +
                "no progress timeout: " +
                error
            );
          }),
        this._request
          .changeMonitorInterval(BITS_IDLE_POLL_RATE_MS)
          .catch(error => {
            LOG(
              "Downloader:_maybeStopActiveNotifications - Failed to decrease " +
                "status update frequency: " +
                error
            );
          }),
      ]);
    }
  }

  /**
   * When the async request begins
   * @param   request
   *          The nsIRequest object for the transfer
   */
  onStartRequest(request) {
    if (this.usingBits) {
      LOG("Downloader:onStartRequest");
    } else {
      LOG(
        "Downloader:onStartRequest - original URI spec: " +
          request.URI.spec +
          ", final URI spec: " +
          request.finalURI.spec
      );
      // Set finalURL in onStartRequest if it is different.
      if (this._patch.finalURL != request.finalURI.spec) {
        this._patch.finalURL = request.finalURI.spec;
        lazy.UM.saveUpdates();
      }
    }

    this.updateService.forEachDownloadListener(listener => {
      listener.onStartRequest(request);
    });
  }

  /**
   * When new data has been downloaded
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   progress
   *          The current number of bytes transferred
   * @param   maxProgress
   *          The total number of bytes that must be transferred
   */
  onProgress(request, progress, maxProgress) {
    LOG("Downloader:onProgress - progress: " + progress + "/" + maxProgress);

    if (progress > this._patch.size) {
      LOG(
        "Downloader:onProgress - progress: " +
          progress +
          " is higher than patch size: " +
          this._patch.size
      );
      AUSTLMY.pingDownloadCode(
        this.isCompleteUpdate,
        AUSTLMY.DWNLD_ERR_PATCH_SIZE_LARGER
      );
      this.cancel(Cr.NS_ERROR_UNEXPECTED);
      return;
    }

    // Wait until the transfer has started (progress > 0) to verify maxProgress
    // so that we don't check it before it is available (in which case, -1 would
    // have been passed).
    if (progress > 0 && maxProgress != this._patch.size) {
      LOG(
        "Downloader:onProgress - maxProgress: " +
          maxProgress +
          " is not equal to expected patch size: " +
          this._patch.size
      );
      AUSTLMY.pingDownloadCode(
        this.isCompleteUpdate,
        AUSTLMY.DWNLD_ERR_PATCH_SIZE_NOT_EQUAL
      );
      this.cancel(Cr.NS_ERROR_UNEXPECTED);
      return;
    }

    this.updateService.forEachDownloadListener(listener => {
      if (listener instanceof Ci.nsIProgressEventSink) {
        listener.onProgress(request, progress, maxProgress);
      }
    });
    this.updateService._consecutiveSocketErrors = 0;
  }

  /**
   * When we have new status text
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   status
   *          A status code
   * @param   statusText
   *          Human readable version of |status|
   */
  onStatus(request, status, statusText) {
    LOG(
      "Downloader:onStatus - status: " + status + ", statusText: " + statusText
    );

    this.updateService.forEachDownloadListener(listener => {
      if (listener instanceof Ci.nsIProgressEventSink) {
        listener.onStatus(request, status, statusText);
      }
    });
  }

  /**
   * When data transfer ceases
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   status
   *          Status code containing the reason for the cessation.
   */
  /* eslint-disable-next-line complexity */
  async onStopRequest(request, status) {
    if (gOnlyDownloadUpdatesThisSession) {
      LOG(
        "Downloader:onStopRequest - End of update download detected and " +
          "ignored because we are restricted to update downloads this " +
          "session. We will continue with this update next session."
      );
      // In order to keep the update from progressing past the downloading
      // stage, we will pretend that the download is still going.
      // A lot of this work is done for us by just not setting this._request to
      // null, which usually signals that the transfer has completed.
      this._pretendingDownloadIsNotDone = true;
      // This notification is currently used only for testing.
      Services.obs.notifyObservers(null, "update-download-restriction-hit");
      return;
    }

    if (!this.usingBits) {
      LOG(
        "Downloader:onStopRequest - downloader: nsIIncrementalDownload, " +
          "original URI spec: " +
          request.URI.spec +
          ", final URI spec: " +
          request.finalURI.spec +
          ", status: " +
          status
      );
    } else {
      LOG("Downloader:onStopRequest - downloader: BITS, status: " + status);
    }

    let bitsCompletionError;
    if (this.usingBits) {
      if (Components.isSuccessCode(status)) {
        try {
          await request.complete();
        } catch (e) {
          LOG(
            "Downloader:onStopRequest - Unable to complete BITS download: " + e
          );
          status = Cr.NS_ERROR_FAILURE;
          bitsCompletionError = e;
        }
      } else {
        // BITS jobs that failed to complete should still have cancel called on
        // them to remove the job.
        try {
          await this.cancel();
        } catch (e) {
          // This will fail if the job stopped because it was cancelled.
          // Even if this is a "real" error, there isn't really anything to do
          // about it, and it's not really a big problem. It just means that the
          // BITS job will stay around until it is removed automatically
          // (default of 90 days).
        }
      }
    }

    var state = this._patch.state;
    var shouldShowPrompt = false;
    var shouldRegisterOnlineObserver = false;
    var shouldRetrySoon = false;
    var deleteActiveUpdate = false;
    let migratedToReadyUpdate = false;
    let nonDownloadFailure = false;
    var retryTimeout = Services.prefs.getIntPref(
      PREF_APP_UPDATE_SOCKET_RETRYTIMEOUT,
      DEFAULT_SOCKET_RETRYTIMEOUT
    );
    // Prevent the preference from setting a value greater than 10000.
    retryTimeout = Math.min(retryTimeout, 10000);
    var maxFail = Services.prefs.getIntPref(
      PREF_APP_UPDATE_SOCKET_MAXERRORS,
      DEFAULT_SOCKET_MAX_ERRORS
    );
    // Prevent the preference from setting a value greater than 20.
    maxFail = Math.min(maxFail, 20);
    LOG(
      "Downloader:onStopRequest - status: " +
        status +
        ", " +
        "current fail: " +
        this.updateService._consecutiveSocketErrors +
        ", " +
        "max fail: " +
        maxFail +
        ", " +
        "retryTimeout: " +
        retryTimeout
    );
    if (Components.isSuccessCode(status)) {
      if (this._verifyDownload()) {
        AUSTLMY.pingDownloadCode(this.isCompleteUpdate, AUSTLMY.DWNLD_SUCCESS);

        LOG(
          "Downloader:onStopRequest - Clearing readyUpdate in preparation of " +
            "moving downloadingUpdate into readyUpdate."
        );

        // Clear out any old update before we notify anyone about the new one.
        // It will be invalid in a moment anyways when we call
        // `cleanUpReadyUpdateDir()`.
        lazy.UM.internal.readyUpdate = null;

        // We're about to clobber the ready update so we can replace it with the
        // downloading update that just finished. We need to let observers know
        // about this.
        if (
          lazy.AUS.currentState == Ci.nsIApplicationUpdateService.STATE_PENDING
        ) {
          transitionState(Ci.nsIApplicationUpdateService.STATE_SWAP);
        }
        Services.obs.notifyObservers(this._update, "update-swap");

        // Swap the downloading update into the ready update directory.
        cleanUpReadyUpdateDir();
        let downloadedMar = getDownloadingUpdateDir();
        downloadedMar.append(FILE_UPDATE_MAR);
        let readyDir = getReadyUpdateDir();
        try {
          downloadedMar.moveTo(readyDir, FILE_UPDATE_MAR);
          migratedToReadyUpdate = true;
        } catch (e) {
          migratedToReadyUpdate = false;
        }

        if (migratedToReadyUpdate) {
          AUSTLMY.pingMoveResult(AUSTLMY.MOVE_RESULT_SUCCESS);
          state = getBestPendingState();
          shouldShowPrompt = !getCanStageUpdates();

          // Tell the updater.exe we're ready to apply.
          LOG(
            `Downloader:onStopRequest - Ready to apply. Setting state to ` +
              `"${state}".`
          );
          writeStatusFile(getReadyUpdateDir(), state);
          writeVersionFile(getReadyUpdateDir(), this._update.appVersion);
          this._update.installDate = new Date().getTime();
          this._update.statusText =
            lazy.gUpdateBundle.GetStringFromName("installPending");
          Services.prefs.setIntPref(PREF_APP_UPDATE_DOWNLOAD_ATTEMPTS, 0);
        } else {
          LOG(
            "Downloader:onStopRequest - failed to move the downloading " +
              "update to the ready update directory."
          );
          AUSTLMY.pingMoveResult(AUSTLMY.MOVE_RESULT_UNKNOWN_FAILURE);

          state = STATE_DOWNLOAD_FAILED;
          status = Cr.NS_ERROR_FILE_COPY_OR_MOVE_FAILED;

          const mfCode = "move_failed";
          let message = getStatusTextFromCode(mfCode, mfCode);
          this._update.statusText = message;

          nonDownloadFailure = true;
          deleteActiveUpdate = true;

          cleanUpDownloadingUpdateDir();
        }
      } else {
        LOG("Downloader:onStopRequest - download verification failed");
        state = STATE_DOWNLOAD_FAILED;
        status = Cr.NS_ERROR_CORRUPTED_CONTENT;

        // Yes, this code is a string.
        const vfCode = "verification_failed";
        var message = getStatusTextFromCode(vfCode, vfCode);
        this._update.statusText = message;

        if (this._update.isCompleteUpdate || this._update.patchCount != 2) {
          LOG("Downloader:onStopRequest - No alternative patch to try");
          deleteActiveUpdate = true;
        }

        // Destroy the updates directory, since we're done with it.
        cleanUpDownloadingUpdateDir();
      }
    } else if (status == Cr.NS_ERROR_OFFLINE) {
      // Register an online observer to try again.
      // The online observer will continue the incremental download by
      // calling downloadUpdate on the active update which continues
      // downloading the file from where it was.
      LOG("Downloader:onStopRequest - offline, register online observer: true");
      AUSTLMY.pingDownloadCode(
        this.isCompleteUpdate,
        AUSTLMY.DWNLD_RETRY_OFFLINE
      );
      shouldRegisterOnlineObserver = true;
      deleteActiveUpdate = false;

      // Each of NS_ERROR_NET_TIMEOUT, ERROR_CONNECTION_REFUSED,
      // NS_ERROR_NET_RESET and NS_ERROR_DOCUMENT_NOT_CACHED can be returned
      // when disconnecting the internet while a download of a MAR is in
      // progress.  There may be others but I have not encountered them during
      // testing.
    } else if (
      (status == Cr.NS_ERROR_NET_TIMEOUT ||
        status == Cr.NS_ERROR_CONNECTION_REFUSED ||
        status == Cr.NS_ERROR_NET_RESET ||
        status == Cr.NS_ERROR_DOCUMENT_NOT_CACHED) &&
      this.updateService._consecutiveSocketErrors < maxFail
    ) {
      LOG("Downloader:onStopRequest - socket error, shouldRetrySoon: true");
      let dwnldCode = AUSTLMY.DWNLD_RETRY_CONNECTION_REFUSED;
      if (status == Cr.NS_ERROR_NET_TIMEOUT) {
        dwnldCode = AUSTLMY.DWNLD_RETRY_NET_TIMEOUT;
      } else if (status == Cr.NS_ERROR_NET_RESET) {
        dwnldCode = AUSTLMY.DWNLD_RETRY_NET_RESET;
      } else if (status == Cr.NS_ERROR_DOCUMENT_NOT_CACHED) {
        dwnldCode = AUSTLMY.DWNLD_ERR_DOCUMENT_NOT_CACHED;
      }
      AUSTLMY.pingDownloadCode(this.isCompleteUpdate, dwnldCode);
      shouldRetrySoon = true;
      deleteActiveUpdate = false;
    } else if (status != Cr.NS_BINDING_ABORTED && status != Cr.NS_ERROR_ABORT) {
      if (
        status == Cr.NS_ERROR_FILE_ACCESS_DENIED ||
        status == Cr.NS_ERROR_FILE_READ_ONLY
      ) {
        LOG("Downloader:onStopRequest - permission error");
        nonDownloadFailure = true;
      } else {
        LOG("Downloader:onStopRequest - non-verification failure");
      }

      let dwnldCode = AUSTLMY.DWNLD_ERR_BINDING_ABORTED;
      if (status == Cr.NS_ERROR_ABORT) {
        dwnldCode = AUSTLMY.DWNLD_ERR_ABORT;
      }
      AUSTLMY.pingDownloadCode(this.isCompleteUpdate, dwnldCode);

      // Some sort of other failure, log this in the |statusText| property
      state = STATE_DOWNLOAD_FAILED;

      // XXXben - if |request| (The Incremental Download) provided a means
      // for accessing the http channel we could do more here.

      this._update.statusText = getStatusTextFromCode(
        status,
        Cr.NS_BINDING_FAILED
      );

      // Destroy the updates directory, since we're done with it.
      cleanUpDownloadingUpdateDir();

      deleteActiveUpdate = true;
    }
    if (!this.usingBits) {
      LOG(`Downloader:onStopRequest - Setting internalResult to ${status}`);
      this._patch.setProperty("internalResult", status);
    } else {
      LOG(`Downloader:onStopRequest - Setting bitsResult to ${status}`);
      this._patch.setProperty("bitsResult", status);

      // If we failed when using BITS, we want to override the retry decision
      // since we need to retry with nsIncrementalDownload before we give up.
      // However, if the download was cancelled, don't retry. If the transfer
      // was cancelled, we don't want it to restart on its own.
      if (
        !Components.isSuccessCode(status) &&
        status != Cr.NS_BINDING_ABORTED &&
        status != Cr.NS_ERROR_ABORT
      ) {
        deleteActiveUpdate = false;
        shouldRetrySoon = true;
      }

      // Send BITS Telemetry
      if (Components.isSuccessCode(status)) {
        AUSTLMY.pingBitsSuccess(this.isCompleteUpdate);
      } else {
        let error;
        if (bitsCompletionError) {
          error = bitsCompletionError;
        } else if (status == Cr.NS_ERROR_CORRUPTED_CONTENT) {
          error = new BitsVerificationError();
        } else {
          error = request.transferError;
          if (!error) {
            error = new BitsUnknownError();
          }
        }
        AUSTLMY.pingBitsError(this.isCompleteUpdate, error);
      }
    }

    LOG("Downloader:onStopRequest - setting state to: " + state);
    if (this._patch.state != state) {
      this._patch.state = state;
    }
    if (deleteActiveUpdate) {
      LOG("Downloader:onStopRequest - Clearing downloadingUpdate.");
      this._update.installDate = new Date().getTime();
      lazy.UM.internal.addUpdateToHistory(lazy.UM.internal.downloadingUpdate);
      lazy.UM.internal.downloadingUpdate = null;
    } else if (
      lazy.UM.internal.downloadingUpdate &&
      lazy.UM.internal.downloadingUpdate.state != state
    ) {
      lazy.UM.internal.downloadingUpdate.state = state;
    }
    if (migratedToReadyUpdate) {
      LOG(
        "Downloader:onStopRequest - Moving downloadingUpdate into readyUpdate"
      );
      lazy.UM.internal.readyUpdate = lazy.UM.internal.downloadingUpdate;
      lazy.UM.internal.downloadingUpdate = null;
    }
    lazy.UM.saveUpdates();

    // Only notify listeners about the stopped state if we
    // aren't handling an internal retry.
    if (!shouldRetrySoon && !shouldRegisterOnlineObserver) {
      this.updateService.forEachDownloadListener(listener => {
        listener.onStopRequest(request, status);
      });
    }

    this._request = null;

    // This notification must happen after _request is set to null so that
    // the correct this.updateService.isDownloading value is available in
    // _notifyDownloadStatusObservers().
    this._notifyDownloadStatusObservers();

    if (state == STATE_DOWNLOAD_FAILED) {
      var allFailed = true;
      // Don't bother retrying the download if we got an error that isn't
      // download related.
      if (!nonDownloadFailure) {
        // If we haven't already, attempt to download without BITS
        if (request instanceof BitsRequest) {
          LOG(
            "Downloader:onStopRequest - BITS download failed. Falling back " +
              "to nsIIncrementalDownload"
          );
          let result = await this.downloadUpdate(this._update);
          if (result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS) {
            LOG(
              "Downloader:onStopRequest - Failed to fall back to " +
                "nsIIncrementalDownload. Cleaning up downloading update."
            );
            await cleanupDownloadingUpdate();
          } else {
            allFailed = false;
          }
        }

        // Check if there is a complete update patch that can be downloaded.
        if (
          allFailed &&
          !this._update.isCompleteUpdate &&
          this._update.patchCount == 2
        ) {
          LOG(
            "Downloader:onStopRequest - verification of patch failed, " +
              "downloading complete update patch"
          );
          this._update.isCompleteUpdate = true;
          let result = await this.downloadUpdate(this._update);

          if (result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS) {
            LOG(
              "Downloader:onStopRequest - Failed to fall back to complete " +
                "patch. Cleaning up downloading update."
            );
            await cleanupDownloadingUpdate();
          } else {
            allFailed = false;
          }
        }
      }

      if (allFailed) {
        let downloadAttempts = Services.prefs.getIntPref(
          PREF_APP_UPDATE_DOWNLOAD_ATTEMPTS,
          0
        );
        downloadAttempts++;
        Services.prefs.setIntPref(
          PREF_APP_UPDATE_DOWNLOAD_ATTEMPTS,
          downloadAttempts
        );
        let maxAttempts = Math.min(
          Services.prefs.getIntPref(PREF_APP_UPDATE_DOWNLOAD_MAXATTEMPTS, 2),
          10
        );

        transitionState(Ci.nsIApplicationUpdateService.STATE_IDLE);

        if (downloadAttempts > maxAttempts) {
          LOG(
            "Downloader:onStopRequest - notifying observers of error. " +
              "topic: update-error, status: download-attempts-exceeded, " +
              "downloadAttempts: " +
              downloadAttempts +
              " " +
              "maxAttempts: " +
              maxAttempts
          );
          Services.obs.notifyObservers(
            this._update,
            "update-error",
            "download-attempts-exceeded"
          );
        } else {
          this._update.selectedPatch.selected = false;
          LOG(
            "Downloader:onStopRequest - notifying observers of error. " +
              "topic: update-error, status: download-attempt-failed"
          );
          Services.obs.notifyObservers(
            this._update,
            "update-error",
            "download-attempt-failed"
          );
        }
        // We don't care about language pack updates now.
        this._langPackTimeout = null;
        LangPackUpdates.delete(unwrap(this._update));

        // Prevent leaking the update object (bug 454964).
        this._update = null;

        // allFailed indicates that we didn't (successfully) call downloadUpdate
        // to try to download a different MAR. In this case, this Downloader
        // is no longer being used.
        this.updateService._downloader = null;
      }
      // A complete download has been initiated or the failure was handled.
      return;
    }

    // If the download has succeeded or failed, we are done with this Downloader
    // object. However, in some cases (ex: network disconnection), we will
    // attempt to resume using this same Downloader.
    if (state != STATE_DOWNLOADING) {
      this.updateService._downloader = null;
    }

    if (
      state == STATE_PENDING ||
      state == STATE_PENDING_SERVICE ||
      state == STATE_PENDING_ELEVATE
    ) {
      if (getCanStageUpdates()) {
        LOG(
          "Downloader:onStopRequest - attempting to stage update: " +
            this._update.name
        );
        // Stage the update
        let stagingStarted = true;
        try {
          Cc["@mozilla.org/updates/update-processor;1"]
            .createInstance(Ci.nsIUpdateProcessor)
            .processUpdate();
        } catch (e) {
          LOG(
            "Downloader:onStopRequest - failed to stage update. Exception: " + e
          );
          stagingStarted = false;
        }
        if (stagingStarted) {
          transitionState(Ci.nsIApplicationUpdateService.STATE_STAGING);
        } else {
          // Fail gracefully in case the application does not support the update
          // processor service.
          shouldShowPrompt = true;
        }
      }
    }

    // If we're still waiting on language pack updates then run a timer to time
    // out the attempt after an appropriate amount of time.
    if (this._langPackTimeout) {
      // Start a timer to measure how much longer it takes for the language
      // packs to stage.
      this._langPackTimeout.gleanTimerId =
        Glean.update.langpackOvertime.start();

      lazy.setTimeout(
        this._langPackTimeout,
        Services.prefs.getIntPref(
          PREF_APP_UPDATE_LANGPACK_TIMEOUT,
          LANGPACK_UPDATE_DEFAULT_TIMEOUT
        )
      );
    }

    // Do this after *everything* else, since it will likely cause the app
    // to shut down.
    if (shouldShowPrompt) {
      // Wait for language packs to stage before showing any prompt to restart.
      let update = this._update;
      promiseLangPacksUpdated(update).then(() => {
        LOG(
          "Downloader:onStopRequest - Notifying observers that " +
            "an update was downloaded. topic: update-downloaded, status: " +
            update.state
        );
        transitionState(Ci.nsIApplicationUpdateService.STATE_PENDING);
        Services.obs.notifyObservers(update, "update-downloaded", update.state);
      });
    }

    if (shouldRegisterOnlineObserver) {
      LOG("Downloader:onStopRequest - Registering online observer");
      this.updateService._registerOnlineObserver();
    } else if (shouldRetrySoon) {
      LOG("Downloader:onStopRequest - Retrying soon");
      this.updateService._consecutiveSocketErrors++;
      if (this.updateService._retryTimer) {
        this.updateService._retryTimer.cancel();
      }
      this.updateService._retryTimer = Cc[
        "@mozilla.org/timer;1"
      ].createInstance(Ci.nsITimer);
      this.updateService._retryTimer.initWithCallback(
        async () => {
          await this.updateService._attemptResume();
        },
        retryTimeout,
        Ci.nsITimer.TYPE_ONE_SHOT
      );
    } else {
      // Prevent leaking the update object (bug 454964)
      this._update = null;
    }
  }

  /**
   * This function should be called when shutting down so that resources get
   * freed properly.
   */
  async cleanup() {
    if (this.usingBits) {
      if (this._pendingRequest) {
        await this._pendingRequest;
      }
      this._request.shutdown();
    }
  }

  /**
   * See nsIInterfaceRequestor.idl
   */
  getInterface(iid) {
    // The network request may require proxy authentication, so provide the
    // default nsIAuthPrompt if requested.
    if (iid.equals(Ci.nsIAuthPrompt)) {
      var prompt =
        Cc["@mozilla.org/network/default-auth-prompt;1"].createInstance();
      return prompt.QueryInterface(iid);
    }
    throw Components.Exception("", Cr.NS_NOINTERFACE);
  }

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIRequestObserver,
    Ci.nsIProgressEventSink,
    Ci.nsIInterfaceRequestor,
  ]);
}

// On macOS, all browser windows can be closed without Firefox exiting. If it
// is left in this state for a while and an update is pending, we should restart
// Firefox on our own to apply the update. This class will do that
// automatically.
class RestartOnLastWindowClosed {
  #enabled = false;
  #hasShutdown = false;

  #restartTimer = null;
  #restartTimerExpired = false;

  constructor() {
    this.#maybeEnableOrDisable();

    Services.prefs.addObserver(
      PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_ENABLED,
      this
    );
    Services.obs.addObserver(this, "quit-application");
  }

  shutdown() {
    LOG("RestartOnLastWindowClosed.shutdown - Shutting down");
    this.#hasShutdown = true;

    Services.prefs.removeObserver(
      PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_ENABLED,
      this
    );
    Services.obs.removeObserver(this, "quit-application");

    this.#maybeEnableOrDisable();
  }

  get shouldEnable() {
    if (AppConstants.platform != "macosx") {
      return false;
    }
    if (this.#hasShutdown) {
      return false;
    }
    return Services.prefs.getBoolPref(
      PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_ENABLED,
      false
    );
  }

  get enabled() {
    return this.#enabled;
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "nsPref:changed":
        if (data == PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_ENABLED) {
          this.#maybeEnableOrDisable();
        }
        break;
      case "quit-application":
        this.shutdown();
        break;
      case "domwindowclosed":
        this.#onWindowClose();
        break;
      case "domwindowopened":
        this.#onWindowOpen();
        break;
      case "update-downloaded":
      case "update-staged":
        this.#onUpdateReady(data);
        break;
    }
  }

  // Returns true if any windows are open. Otherwise, false.
  #windowsAreOpen() {
    // eslint-disable-next-line no-unused-vars
    for (const win of Services.wm.getEnumerator(null)) {
      return true;
    }
    return false;
  }

  // Enables or disables this class's functionality based on the value of
  // this.shouldEnable. Does nothing if the class is already in the right state
  // (i.e. if the class should be enabled and already is, or should be disabled
  // and already is).
  #maybeEnableOrDisable() {
    if (this.shouldEnable) {
      if (this.#enabled) {
        return;
      }
      LOG("RestartOnLastWindowClosed.#maybeEnableOrDisable - Enabling");

      Services.obs.addObserver(this, "domwindowclosed");
      Services.obs.addObserver(this, "domwindowopened");
      Services.obs.addObserver(this, "update-downloaded");
      Services.obs.addObserver(this, "update-staged");

      this.#restartTimer = null;
      this.#restartTimerExpired = false;

      this.#enabled = true;

      // Synchronize with external state.
      this.#onWindowClose();
    } else {
      if (!this.#enabled) {
        return;
      }
      LOG("RestartOnLastWindowClosed.#maybeEnableOrDisable - Disabling");

      Services.obs.removeObserver(this, "domwindowclosed");
      Services.obs.removeObserver(this, "domwindowopened");
      Services.obs.removeObserver(this, "update-downloaded");
      Services.obs.removeObserver(this, "update-staged");

      this.#enabled = false;

      if (this.#restartTimer) {
        this.#restartTimer.cancel();
      }
      this.#restartTimer = null;
    }
  }

  // Note: Since we keep track of the update state even when this class is
  //       disabled, this function will run even in that case.
  #onUpdateReady(updateState) {
    // Note that we do not count pending-elevate as a ready state, because we
    // cannot silently restart in that state.
    if (
      [
        STATE_APPLIED,
        STATE_PENDING,
        STATE_APPLIED_SERVICE,
        STATE_PENDING_SERVICE,
      ].includes(updateState)
    ) {
      if (this.#enabled) {
        LOG("RestartOnLastWindowClosed.#onUpdateReady - update ready");
        this.#maybeRestartBrowser();
      }
    } else if (this.#enabled) {
      LOG(
        `RestartOnLastWindowClosed.#onUpdateReady - Not counting update as ` +
          `ready because the state is ${updateState}`
      );
    }
  }

  #onWindowClose() {
    if (!this.#windowsAreOpen()) {
      this.#onLastWindowClose();
    }
  }

  #onLastWindowClose() {
    if (this.#restartTimer || this.#restartTimerExpired) {
      LOG(
        "RestartOnLastWindowClosed.#onLastWindowClose - Restart timer is " +
          "either already running or has already expired"
      );
      return;
    }

    let timeout = Services.prefs.getIntPref(
      PREF_APP_UPDATE_NO_WINDOW_AUTO_RESTART_DELAY_MS,
      5 * 60 * 1000
    );

    LOG(
      "RestartOnLastWindowClosed.#onLastWindowClose - Last window closed. " +
        "Starting restart timer"
    );
    this.#restartTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    this.#restartTimer.initWithCallback(
      () => this.#onRestartTimerExpire(),
      timeout,
      Ci.nsITimer.TYPE_ONE_SHOT
    );
  }

  #onWindowOpen() {
    if (this.#restartTimer) {
      LOG(
        "RestartOnLastWindowClosed.#onWindowOpen - Window opened. Cancelling " +
          "restart timer."
      );
      this.#restartTimer.cancel();
    }
    this.#restartTimer = null;
    this.#restartTimerExpired = false;
  }

  #onRestartTimerExpire() {
    LOG("RestartOnLastWindowClosed.#onRestartTimerExpire - Timer Expired");

    this.#restartTimer = null;
    this.#restartTimerExpired = true;
    this.#maybeRestartBrowser();
  }

  #maybeRestartBrowser() {
    if (!this.#restartTimerExpired) {
      LOG(
        "RestartOnLastWindowClosed.#maybeRestartBrowser - Still waiting for " +
          "all windows to be closed and restartTimer to expire. " +
          "(not restarting)"
      );
      return;
    }

    if (lazy.AUS.currentState != Ci.nsIApplicationUpdateService.STATE_PENDING) {
      LOG(
        "RestartOnLastWindowClosed.#maybeRestartBrowser - No update ready. " +
          "(not restarting)"
      );
      return;
    }

    if (getElevationRequired()) {
      // We check for STATE_PENDING_ELEVATE elsewhere, but this is actually
      // different from that because it is technically possible that the user
      // gave permission to elevate, but we haven't actually elevated yet.
      // This is a bit of a corner case. We only call elevationOptedIn() right
      // before we restart to apply the update immediately. But it is possible
      // that something could stop the browser from shutting down.
      LOG(
        "RestartOnLastWindowClosed.#maybeRestartBrowser - This update will " +
          "require user elevation (not restarting)"
      );
      return;
    }

    if (this.#windowsAreOpen()) {
      LOG(
        "RestartOnLastWindowClosed.#maybeRestartBrowser - Window " +
          "unexpectedly still open! (not restarting)"
      );
      return;
    }

    if (!this.shouldEnable) {
      LOG(
        "RestartOnLastWindowClosed.#maybeRestartBrowser - Unexpectedly " +
          "attempted to restart when RestartOnLastWindowClosed ought to be " +
          "disabled! (not restarting)"
      );
      return;
    }

    LOG("RestartOnLastWindowClosed.#maybeRestartBrowser - Restarting now");
    Glean.update.noWindowAutoRestarts.add(1);
    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit |
        Ci.nsIAppStartup.eRestart |
        Ci.nsIAppStartup.eSilently
    );
  }
}
// Nothing actually uses this variable at the moment, but let's make sure that
// we hold the reference to the RestartOnLastWindowClosed instance somewhere.
// eslint-disable-next-line no-unused-vars
let restartOnLastWindowClosed = new RestartOnLastWindowClosed();
