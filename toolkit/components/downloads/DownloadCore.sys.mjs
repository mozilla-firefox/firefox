/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Main implementation of the Downloads API objects. Consumers should get
 * references to these objects through the "Downloads.sys.mjs" module.
 */

import { Integration } from "resource://gre/modules/Integration.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DownloadHistory: "resource://gre/modules/DownloadHistory.sys.mjs",
  DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gExternalAppLauncher",
  "@mozilla.org/uriloader/external-helper-app-service;1",
  Ci.nsPIExternalAppLauncher
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gExternalHelperAppService",
  "@mozilla.org/uriloader/external-helper-app-service;1",
  Ci.nsIExternalHelperAppService
);

Integration.downloads.defineESModuleGetter(
  lazy,
  "DownloadIntegration",
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);

const BackgroundFileSaverStreamListener = Components.Constructor(
  "@mozilla.org/network/background-file-saver;1?mode=streamlistener",
  "nsIBackgroundFileSaver"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enableDeletePrivateFeature",
  "browser.download.enableDeletePrivate",
  false
);

/**
 * Returns true if the given value is a primitive string or a String object.
 */
function isString(aValue) {
  // We cannot use the "instanceof" operator reliably across module boundaries.
  return (
    typeof aValue == "string" ||
    (typeof aValue == "object" && "charAt" in aValue)
  );
}

/**
 * Serialize the unknown properties of aObject into aSerializable.
 */
function serializeUnknownProperties(aObject, aSerializable) {
  if (aObject._unknownProperties) {
    for (let property in aObject._unknownProperties) {
      aSerializable[property] = aObject._unknownProperties[property];
    }
  }
}

/**
 * Check for any unknown properties in aSerializable and preserve those in the
 * _unknownProperties field of aObject. aFilterFn is called for each property
 * name of aObject and should return true only for unknown properties.
 */
function deserializeUnknownProperties(aObject, aSerializable, aFilterFn) {
  for (let property in aSerializable) {
    if (aFilterFn(property)) {
      if (!aObject._unknownProperties) {
        aObject._unknownProperties = {};
      }

      aObject._unknownProperties[property] = aSerializable[property];
    }
  }
}

/**
 * Check if the file is a placeholder.
 *
 * @returns {Promise<boolean>}
 */
async function isPlaceholder(path) {
  try {
    if ((await IOUtils.stat(path)).size == 0) {
      return true;
    }
  } catch (ex) {
    // Canceling the download may have removed the placeholder already.
    if (ex.name != "NotFoundError") {
      console.error(ex);
    }
  }
  return false;
}

/**
 * This determines the minimum time interval between updates to the number of
 * bytes transferred, and is a limiting factor to the sequence of readings used
 * in calculating the speed of the download.
 */
const kProgressUpdateIntervalMs = 400;

/**
 * These sets represent the current download batch in public and private
 * contexts.
 */
const gPublicBatch = new Set(),
  gPrivateBatch = new Set();

/**
 * Represents a single download, with associated state and actions.  This object
 * is transient, though it can be included in a DownloadList so that it can be
 * managed by the user interface and persisted across sessions.
 */
export var Download = function () {
  this._deferSucceeded = Promise.withResolvers();
};

Download.prototype = {
  /**
   * DownloadSource object associated with this download.
   */
  source: null,

  /**
   * DownloadTarget object associated with this download.
   */
  target: null,

  /**
   * DownloadSaver object associated with this download.
   */
  saver: null,

  /**
   * Indicates that the download never started, has been completed successfully,
   * failed, or has been canceled.  This property becomes false when a download
   * is started for the first time, or when a failed or canceled download is
   * restarted.
   */
  stopped: true,

  /**
   * Indicates that the download has been completed successfully.
   */
  succeeded: false,

  /**
   * Indicates that the download has been canceled.  This property can become
   * true, then it can be reset to false when a canceled download is restarted.
   *
   * This property becomes true as soon as the "cancel" method is called, though
   * the "stopped" property might remain false until the cancellation request
   * has been processed.  Temporary files or part files may still exist even if
   * they are expected to be deleted, until the "stopped" property becomes true.
   */
  canceled: false,

  /**
   * Downloaded files can be deleted from within Firefox, e.g. via the context
   * menu. Currently Firefox does not track file moves (see bug 1746386), so if
   * a download's target file stops existing we have to assume it's "moved or
   * missing." To distinguish files intentionally deleted within Firefox from
   * files that are moved/missing, we mark them as "deleted" with this property.
   */
  deleted: false,

  /**
   * When the download fails, this is set to a DownloadError instance indicating
   * the cause of the failure.  If the download has been completed successfully
   * or has been canceled, this property is null.  This property is reset to
   * null when a failed download is restarted.
   */
  error: null,

  /**
   * Indicates the start time of the download.  When the download starts,
   * this property is set to a valid Date object.  The default value is null
   * before the download starts.
   */
  startTime: null,

  /**
   * Indicates whether this download's "progress" property is able to report
   * partial progress while the download proceeds, and whether the value in
   * totalBytes is relevant.  This depends on the saver and the download source.
   */
  hasProgress: false,

  /**
   * Progress percent, from 0 to 100.  Intermediate values are reported only if
   * hasProgress is true.
   *
   * Note: You shouldn't rely on this property being equal to 100 to determine
   *       whether the download is completed.  You should use the individual
   *       state properties instead.
   */
  progress: 0,

  /**
   * When hasProgress is true, indicates the total number of bytes to be
   * transferred before the download finishes, that can be zero for empty files.
   *
   * When hasProgress is false, this property is always zero.
   *
   * Note: This property may be different than the final file size on disk for
   *       downloads that are encoded during the network transfer.  You can use
   *       the "size" property of the DownloadTarget object to get the actual
   *       size on disk once the download succeeds.
   */
  totalBytes: 0,

  /**
   * Number of bytes currently transferred.  This value starts at zero, and may
   * be updated regardless of the value of hasProgress.
   *
   * Note: You shouldn't rely on this property being equal to totalBytes to
   *       determine whether the download is completed.  You should use the
   *       individual state properties instead.  This property may not be
   *       updated during the last part of the download.
   */
  currentBytes: 0,

  /**
   * Fractional number representing the speed of the download, in bytes per
   * second.  This value is zero when the download is stopped, and may be
   * updated regardless of the value of hasProgress.
   */
  speed: 0,

  /**
   * Indicates whether, at this time, there is any partially downloaded data
   * that can be used when restarting a failed or canceled download.
   *
   * Even if the download has partial data on disk, hasPartialData will be false
   * if that data cannot be used to restart the download. In order to determine
   * if a part file is being used which contains partial data the
   * Download.target.partFilePath should be checked.
   *
   * This property is relevant while the download is in progress, and also if it
   * failed or has been canceled.  If the download has been completed
   * successfully, this property is always false.
   *
   * Whether partial data can actually be retained depends on the saver and the
   * download source, and may not be known before the download is started.
   */
  hasPartialData: false,

  /**
   * Indicates whether, at this time, there is any data that has been blocked.
   * Since reputation blocking takes place after the download has fully
   * completed a value of true also indicates 100% of the data is present.
   */
  hasBlockedData: false,

  /**
   * This can be set to a function that is called after other properties change.
   */
  onchange: null,

  /**
   * This tells if the user has chosen to open/run the downloaded file after
   * download has completed.
   */
  launchWhenSucceeded: false,

  /**
   * When a download starts, we typically want to automatically open the
   * downloads panel if the pref browser.download.alwaysOpenPanel is enabled.
   * However, there are conditions where we want to prevent this. For example, a
   * false value can prevent the downloads panel from opening when an add-on
   * creates a download without user input as part of some background operation.
   */
  openDownloadsListOnStart: true,

  /**
   * This represents the MIME type of the download.
   */
  contentType: null,

  /**
   * This indicates the path of the application to be used to launch the file,
   * or null if the file should be launched with the default application.
   */
  launcherPath: null,

  /**
   * This contains application id to be used to launch the file,
   * or null if the file is not meant to be launched with GIOHandlerApp.
   */
  launcherId: null,

  /**
   * Any download that is running has this property set to true.  The property
   * remains true until this download is canceled or until all downloads are
   * stopped.  (If this download completes, isInCurrentBatch remains true for
   * as long as any other download is running, even if the running download was
   * started after this download completed.)
   */
  get isInCurrentBatch() {
    return this._batch !== null;
  },

  /**
   * Set containing this object, or null.
   */
  _batch: null,

  /**
   * Raises the onchange notification.
   */
  _notifyChange: function D_notifyChange() {
    try {
      if (this.onchange) {
        this.onchange();
      }
    } catch (ex) {
      console.error(ex);
    }
  },

  /**
   * The download may be stopped and restarted multiple times before it
   * completes successfully. This may happen if any of the download attempts is
   * canceled or fails.
   *
   * This property contains a promise that is linked to the current attempt, or
   * null if the download is either stopped or in the process of being canceled.
   * If the download restarts, this property is replaced with a new promise.
   *
   * The promise is resolved if the attempt it represents finishes successfully,
   * and rejected if the attempt fails.
   */
  _currentAttempt: null,

  /**
   * The download was launched to open from the Downloads Panel.
   */
  _launchedFromPanel: false,

  /**
   * Starts the download for the first time, or restarts a download that failed
   * or has been canceled.
   *
   * Calling this method when the download has been completed successfully has
   * no effect, and the method returns a resolved promise.  If the download is
   * in progress, the method returns the same promise as the previous call.
   *
   * If the "cancel" method was called but the cancellation process has not
   * finished yet, this method waits for the cancellation to finish, then
   * restarts the download immediately.
   *
   * Note: If you need to start a new download from the same source, rather than
   *       restarting a failed or canceled one, you should create a separate
   *       Download object with the same source as the current one.
   *
   * @returns {Promise<void>}
   *   Resolves when the download has finished successfully.
   * @rejects JavaScript exception if the download failed.
   */
  start: async function D_start() {
    // If the download succeeded, it's the final state, we have nothing to do.
    if (this.succeeded) {
      return Promise.resolve();
    }

    // If the download already started and hasn't failed or hasn't been
    // canceled, return the same promise as the previous call, allowing the
    // caller to wait for the current attempt to finish.
    if (this._currentAttempt) {
      return this._currentAttempt;
    }

    // While shutting down or disposing of this object, we prevent the download
    // from returning to be in progress.
    if (this._finalized) {
      return Promise.reject(
        new DownloadError({
          message: "Cannot start after finalization.",
        })
      );
    }

    if (
      this.error?.becauseBlockedByReputationCheck ||
      this.error?.becauseBlockedByContentAnalysis
    ) {
      return Promise.reject(
        new DownloadError({
          message: "Cannot start after being blocked by a safety check.",
        })
      );
    }

    // Initialize all the status properties for a new or restarted download.
    this.stopped = false;
    this.canceled = false;
    if (!this._batch) {
      this._batch = this.source.isPrivate ? gPrivateBatch : gPublicBatch;
      this._batch.add(this);
    }
    this.error = null;
    // Avoid serializing the previous error, or it would be restored on the next
    // startup, even if the download was restarted.
    delete this._unknownProperties?.errorObj;
    this.hasProgress = false;
    this.hasBlockedData = false;
    this.progress = 0;
    this.totalBytes = 0;
    this.currentBytes = 0;
    this.startTime = new Date();

    // Create a new deferred object and an associated promise before starting
    // the actual download.  We store it on the download as the current attempt.
    let deferAttempt = Promise.withResolvers();
    let currentAttempt = deferAttempt.promise;
    this._currentAttempt = currentAttempt;

    // Restart the progress and speed calculations from scratch.
    this._lastProgressTimeMs = 0;

    // This function propagates progress from the DownloadSaver object, unless
    // it comes in late from a download attempt that was replaced by a new one.
    // If the cancellation process for the download has started, then the update
    // is ignored.
    function DS_setProgressBytes(aCurrentBytes, aTotalBytes, aHasPartialData) {
      if (this._currentAttempt == currentAttempt) {
        this._setBytes(aCurrentBytes, aTotalBytes, aHasPartialData);
      }
    }

    // This function propagates download properties from the DownloadSaver
    // object, unless it comes in late from a download attempt that was
    // replaced by a new one.  If the cancellation process for the download has
    // started, then the update is ignored.
    function DS_setProperties(aOptions) {
      if (this._currentAttempt != currentAttempt) {
        return;
      }

      let changeMade = false;

      for (let property of [
        "contentType",
        "progress",
        "hasPartialData",
        "hasBlockedData",
      ]) {
        if (property in aOptions && this[property] != aOptions[property]) {
          this[property] = aOptions[property];
          changeMade = true;
        }
      }

      if (changeMade) {
        this._notifyChange();
      }
    }

    await lazy.DownloadIntegration.determineFilename(this);

    // Now that we stored the promise in the download object, we can start the
    // task that will actually execute the download.
    deferAttempt.resolve(
      (async () => {
        // Wait upon any pending operation before restarting.
        if (this._promiseCanceled) {
          await this._promiseCanceled;
        }
        if (this._promiseRemovePartialData) {
          try {
            await this._promiseRemovePartialData;
          } catch (ex) {
            // Ignore any errors, which are already reported by the original
            // caller of the removePartialData method.
          }
        }

        // In case the download was restarted while cancellation was in progress,
        // but the previous attempt actually succeeded before cancellation could
        // be processed, it is possible that the download has already finished.
        if (this.succeeded) {
          return;
        }

        try {
          if (this.downloadingToSameFile()) {
            throw new DownloadError({
              message: "Can't overwrite the source file.",
              becauseTargetFailed: true,
            });
          }

          // Disallow download if parental controls service restricts it.
          if (
            await lazy.DownloadIntegration.shouldBlockForParentalControls(this)
          ) {
            throw new DownloadError({ becauseBlockedByParentalControls: true });
          }

          // We should check if we have been canceled in the meantime, after all
          // the previous asynchronous operations have been executed and just
          // before we call the "execute" method of the saver.
          if (this._promiseCanceled) {
            // The exception will become a cancellation in the "catch" block.
            throw new Error(undefined);
          }

          // Execute the actual download through the saver object.
          this._saverExecuting = true;
          try {
            await this.saver.execute(
              DS_setProgressBytes.bind(this),
              DS_setProperties.bind(this)
            );
          } catch (ex) {
            // Remove the target file placeholder and all partial data when
            // needed, independently of which code path failed. In some cases, the
            // component executing the download may have already removed the file.
            if (!this.hasPartialData && !this.hasBlockedData) {
              await this.saver.removeData(true);
            }
            throw ex;
          }

          // Now that the actual saving finished, read the actual file size on
          // disk, that may be different from the amount of data transferred.
          await this.target.refresh();

          // Check for the last time if the download has been canceled. This must
          // be done right before setting the "stopped" property of the download,
          // without any asynchronous operations in the middle, so that another
          // cancellation request cannot start in the meantime and stay unhandled.
          if (this._promiseCanceled) {
            // To keep the internal state of the Download object consistent, we
            // just delete the target and effectively cancel the download. Since
            // the DownloadSaver succeeded, we already renamed the ".part" file to
            // the final name, and this results in all the data being deleted.
            await this.saver.removeData(true);

            // Cancellation exceptions will be changed in the catch block below.
            throw new DownloadError();
          }

          // Update the status properties for a successful download.
          this.progress = 100;
          this.succeeded = true;
          this.hasPartialData = false;
        } catch (originalEx) {
          // We may choose a different exception to propagate in the code below,
          // or wrap the original one. We do this mutation in a different variable
          // because of the "no-ex-assign" ESLint rule.
          let ex = originalEx;

          // Fail with a generic status code on cancellation, so that the caller
          // is forced to actually check the status properties to see if the
          // download was canceled or failed because of other reasons.
          if (this._promiseCanceled) {
            throw new DownloadError({ message: "Download canceled." });
          }

          // An HTTP 450 error code is used by Windows to indicate that a uri is
          // blocked by parental controls. This will prevent the download from
          // occuring, so an error needs to be raised. This is not performed
          // during the parental controls check above as it requires the request
          // to start.
          if (this._blockedByParentalControls) {
            ex = new DownloadError({ becauseBlockedByParentalControls: true });
          }

          // Update the download error, unless a new attempt already started. The
          // change in the status property is notified in the finally block.
          if (this._currentAttempt == currentAttempt || !this._currentAttempt) {
            if (!(ex instanceof DownloadError)) {
              let properties = { innerException: ex };

              if (ex.message) {
                properties.message = ex.message;
              }

              ex = new DownloadError(properties);
            }
            // Don't store an error if it's an abort caused by shutdown, so the
            // download can be retried automatically at the next startup.
            if (
              originalEx.result != Cr.NS_ERROR_ABORT ||
              !Services.startup.isInOrBeyondShutdownPhase(
                Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
              )
            ) {
              this.error = ex;
            }
          }
          throw ex;
        } finally {
          // Any cancellation request has now been processed.
          this._saverExecuting = false;
          this._promiseCanceled = null;

          // Update the status properties, unless a new attempt already started.
          if (this._currentAttempt == currentAttempt || !this._currentAttempt) {
            this._currentAttempt = null;
            this.stopped = true;
            this.speed = 0;
            if (!this._batch || Download._updateBatch(this._batch)) {
              this._notifyChange();
            }
            if (this.succeeded) {
              await this._succeed();
            }
          }
        }
      })()
    );

    // Notify the new download state before returning.
    this._notifyChange();
    return currentAttempt;
  },

  /**
   * Perform the actions necessary when a Download succeeds.
   *
   * @returns {Promise<void>}
   *   Resolves when the steps to take after success have completed.
   * @rejects  JavaScript exception if any of the operations failed.
   */
  async _succeed() {
    await lazy.DownloadIntegration.downloadDone(this);

    this._deferSucceeded.resolve();

    if (this.launchWhenSucceeded) {
      this.launch().catch(console.error);

      // If the "have users choose whether to delete private downloads" feature
      // is not enabled, we should delete the temporary file once the private
      // browsing session ends. Otherwise, we'll decide whether to delete the
      // file further down, for all downloads (not just the ones that have
      // `launchWhenSucceeded` set to true).
      // Once we complete rollout of `enableDeletePrivateFeature`, we can
      // remove this block.
      if (!lazy.enableDeletePrivateFeature && this.source.isPrivate) {
        lazy.gExternalAppLauncher.deleteTemporaryPrivateFileWhenPossible(
          new lazy.FileUtils.File(this.target.path)
        );
      }
      if (
        !this.source.isPrivate &&
        Services.prefs.getBoolPref("browser.helperApps.deleteTempFileOnExit") &&
        Services.prefs.getBoolPref(
          "browser.download.start_downloads_in_tmp_dir",
          false
        )
      ) {
        lazy.gExternalAppLauncher.deleteTemporaryFileOnExit(
          new lazy.FileUtils.File(this.target.path)
        );
      }
    }

    if (
      lazy.enableDeletePrivateFeature &&
      Services.prefs.getBoolPref("browser.download.deletePrivate", false) &&
      this.source.isPrivate
    ) {
      lazy.gExternalAppLauncher.deletePrivateFileWhenPossible(
        new lazy.FileUtils.File(this.target.path)
      );
    }
  },

  /**
   * When a request to unblock the download is received, contains a promise
   * that will be resolved when the unblock request is completed. This property
   * will then continue to hold the promise indefinitely.
   */
  _promiseUnblock: null,

  /**
   * When a request to confirm the block of the download is received, contains
   * a promise that will be resolved when cleaning up the download has
   * completed. This property will then continue to hold the promise
   * indefinitely.
   */
  _promiseConfirmBlock: null,

  /**
   * Unblocks a download which had been blocked by reputation.
   *
   * The file will be moved out of quarantine and the download will be
   * marked as succeeded.
   *
   * @returns {Promise<void>}
   *   Resolves when the Download has been unblocked and succeeded.
   * @rejects  JavaScript exception if any of the operations failed.
   */
  unblock() {
    if (this._promiseUnblock) {
      return this._promiseUnblock;
    }

    if (this._promiseConfirmBlock) {
      return Promise.reject(
        new Error("Download block has been confirmed, cannot unblock.")
      );
    }

    if (this.error?.becauseBlockedByReputationCheck) {
      Glean.downloads.userActionOnBlockedDownload[
        this.error.reputationCheckVerdict
      ].accumulateSingleSample(2); // unblock
    }

    if (
      this.error?.reputationCheckVerdict == DownloadError.BLOCK_VERDICT_INSECURE
    ) {
      // In this Error case, the download was actually canceled before it was
      // passed to the Download UI. So we need to start the download here.
      this.error = null;
      this.succeeded = false;
      this.hasBlockedData = false;
      // This ensures the verdict will not get set again after the browser
      // restarts and the download gets serialized and de-serialized again.
      delete this._unknownProperties?.errorObj;
      this.start()
        .catch(err => {
          if (err.becauseTargetFailed) {
            // In case we cannot write to the target file
            // retry with a new unique name
            let uniquePath = lazy.DownloadPaths.createNiceUniqueFile(
              new lazy.FileUtils.File(this.target.path)
            ).path;
            this.target.path = uniquePath;
            return this.start();
          }
          return Promise.reject(err);
        })
        .catch(err => {
          if (!this.canceled) {
            console.error(err);
          }
          this._notifyChange();
        });
      this._notifyChange();
      this._promiseUnblock = lazy.DownloadIntegration.downloadDone(this);
      return this._promiseUnblock;
    }

    if (this.error?.becauseBlockedByContentAnalysis) {
      this.respondToContentAnalysisWarnWithAllow();
    }

    if (!this.hasBlockedData) {
      return Promise.reject(
        new Error("unblock may only be called on Downloads with blocked data.")
      );
    }

    this._promiseUnblock = (async () => {
      try {
        if (this.target.partFilePath) {
          await IOUtils.move(this.target.partFilePath, this.target.path);
        }
        await this.target.refresh();
      } catch (ex) {
        await this.refresh();
        this._promiseUnblock = null;
        throw ex;
      }

      this.succeeded = true;
      this.hasBlockedData = false;
      this._notifyChange();
      await this._succeed();
    })();

    return this._promiseUnblock;
  },

  /**
   * Indicates that the download should be allowed. Will do nothing
   * if content analysis was not used.
   */
  respondToContentAnalysisWarnWithAllow() {
    if (this.error?.contentAnalysisWarnRequestToken) {
      lazy.DownloadIntegration.getContentAnalysisService().respondToWarnDialog(
        this.error.contentAnalysisWarnRequestToken,
        true
      );
      this.error.contentAnalysisWarnRequestToken = undefined;
    }
  },

  /**
   * Indicates that the download should be blocked. Will do nothing
   * if content analysis was not used.
   */
  async respondToContentAnalysisWarnWithBlock() {
    if (this.error?.contentAnalysisWarnRequestToken) {
      lazy.DownloadIntegration.getContentAnalysisService().respondToWarnDialog(
        this.error.contentAnalysisWarnRequestToken,
        false
      );
      this.error.contentAnalysisWarnRequestToken = undefined;
      if (!this.target.partFilePath) {
        // Callers will be finalizing the download after this.
        // But if the download happened in place, we need to
        // remove the final target file.
        try {
          await this.saver.removeData(true);
        } catch (ex) {
          console.error(ex);
        }
      }
    }
  },

  /**
   * Confirms that a blocked download should be cleaned up.
   *
   * If a download was blocked but retained on disk this method can be used
   * to remove the file.
   *
   * @returns {Promise<void>}
   *   Resolves when the Download's data has been removed.
   * @rejects  JavaScript exception if any of the operations failed.
   */
  confirmBlock() {
    if (this._promiseConfirmBlock) {
      return this._promiseConfirmBlock;
    }

    if (this._promiseUnblock) {
      return Promise.reject(
        new Error("Download is being unblocked, cannot confirmBlock.")
      );
    }

    if (this.error?.becauseBlockedByReputationCheck) {
      Glean.downloads.userActionOnBlockedDownload[
        this.error.reputationCheckVerdict
      ].accumulateSingleSample(1); // confirm block
    }

    if (!this.hasBlockedData) {
      return Promise.reject(
        new Error(
          "confirmBlock may only be called on Downloads with blocked data."
        )
      );
    }

    this._promiseConfirmBlock = (async () => {
      if (this.error?.becauseBlockedByContentAnalysis) {
        await this.respondToContentAnalysisWarnWithBlock();
      }
      // This call never throws exceptions. If the removal fails, the blocked
      // data remains stored on disk in the ".part" file.
      await this.saver.removeData();

      this.deleted = true;
      this.hasBlockedData = false;
      this._notifyChange();
    })();

    return this._promiseConfirmBlock;
  },

  /**
   * Launches the file after download has completed. This can open
   * the file with the default application for the target MIME type
   * or file extension, or with a custom application if launcherPath
   * or launcherId is set.
   *
   * @param options.openWhere  Optional string indicating how to open when handling
   *                           download by opening the target file URI.
   *                           One of "window", "tab", "tabshifted"
   * @param options.useSystemDefault
   *                           Optional value indicating how to handle launching this download,
   *                           this time only. Will override the associated mimeInfo.preferredAction
   * @returns {Promise<void>}
   *   Resolves when the instruction to launch the file has been
   *           successfully given to the operating system. Note that
   *           the OS might still take a while until the file is actually
   *           launched.
   * @rejects  JavaScript exception if there was an error trying to launch
   *           the file.
   */
  launch(options = {}) {
    if (!this.succeeded) {
      return Promise.reject(
        new Error("launch can only be called if the download succeeded")
      );
    }

    if (this._launchedFromPanel) {
      Glean.downloads.fileOpened.add(1);
    }

    return lazy.DownloadIntegration.launchDownload(this, options);
  },

  /**
   * Shows the folder containing the target file, or where the target file
   * will be saved. This may be called at any time, even if the download
   * failed or is currently in progress.
   *
   * @returns {Promise<void>}
   *   Resolves when the instruction to open the containing folder has been
   *           successfully given to the operating system. Note that
   *           the OS might still take a while until the folder is actually
   *           opened.
   * @rejects  JavaScript exception if there was an error trying to open
   *           the containing folder.
   */
  showContainingDirectory: function D_showContainingDirectory() {
    return lazy.DownloadIntegration.showContainingDirectory(this.target.path);
  },

  /**
   * When a request to cancel the download is received, contains a promise that
   * will be resolved when the cancellation request is processed.  When the
   * request is processed, this property becomes null again.
   */
  _promiseCanceled: null,

  /**
   * True between the call to the "execute" method of the saver and the
   * completion of the current download attempt.
   */
  _saverExecuting: false,

  /**
   * Cancels the download.
   *
   * The cancellation request is asynchronous.  Until the cancellation process
   * finishes, temporary files or part files may still exist even if they are
   * expected to be deleted.
   *
   * In case the download completes successfully before the cancellation request
   * could be processed, this method has no effect, and it returns a resolved
   * promise.  You should check the properties of the download at the time the
   * returned promise is resolved to determine if the download was cancelled.
   *
   * Calling this method when the download has been completed successfully,
   * failed, or has been canceled has no effect, and the method returns a
   * resolved promise.  This behavior is designed for the case where the call
   * to "cancel" happens asynchronously, and is consistent with the case where
   * the cancellation request could not be processed in time.
   *
   * @returns {Promise<void>}
   *   Resolves when the cancellation process has finished.
   * @rejects Never.
   */
  cancel: function D_cancel() {
    // If the download is currently stopped, we have nothing to do.
    if (this.stopped) {
      return Promise.resolve();
    }

    if (!this._promiseCanceled) {
      // Start a new cancellation request.
      this._promiseCanceled = new Promise(resolve => {
        this._currentAttempt.then(resolve, resolve);
      });

      // The download can already be restarted.
      this._currentAttempt = null;

      // Notify that the cancellation request was received.
      this.canceled = true;
      let batch = this._batch;
      this._batch = null;
      batch.delete(this);
      Download._updateBatch(batch);
      this._notifyChange();

      // Execute the actual cancellation through the saver object, in case it
      // has already started.  Otherwise, the cancellation will be handled just
      // before the saver is started.
      if (this._saverExecuting) {
        this.saver.cancel();
      }
    }

    return this._promiseCanceled;
  },

  /**
   * Indicates whether any partially downloaded data should be retained, to use
   * when restarting a failed or canceled download.  The default is false.
   *
   * Whether partial data can actually be retained depends on the saver and the
   * download source, and may not be known before the download is started.
   *
   * To have any effect, this property must be set before starting the download.
   * Resetting this property to false after the download has already started
   * will not remove any partial data.
   *
   * If this property is set to true, care should be taken that partial data is
   * removed before the reference to the download is discarded.  This can be
   * done using the removePartialData or the "finalize" methods.
   */
  tryToKeepPartialData: false,

  /**
   * When a request to remove partially downloaded data is received, contains a
   * promise that will be resolved when the removal request is processed.  When
   * the request is processed, this property becomes null again.
   */
  _promiseRemovePartialData: null,

  /**
   * Removes any partial data kept as part of a canceled or failed download.
   *
   * If the download is not canceled or failed, this method has no effect, and
   * it returns a resolved promise.  If the "cancel" method was called but the
   * cancellation process has not finished yet, this method waits for the
   * cancellation to finish, then removes the partial data.
   *
   * After this method has been called, if the tryToKeepPartialData property is
   * still true when the download is restarted, partial data will be retained
   * during the new download attempt.
   *
   * @returns {Promise<void>}
   *   Resolves when the partial data has been successfully removed.
   * @rejects JavaScript exception if the operation could not be completed.
   */
  removePartialData() {
    if (!this.canceled && !this.error) {
      return Promise.resolve();
    }

    if (!this._promiseRemovePartialData) {
      this._promiseRemovePartialData = (async () => {
        try {
          // Wait upon any pending cancellation request.
          if (this._promiseCanceled) {
            await this._promiseCanceled;
          }
          // Ask the saver object to remove any partial data.
          await this.saver.removeData();
          // For completeness, clear the number of bytes transferred.
          if (this.currentBytes != 0 || this.hasPartialData) {
            this.currentBytes = 0;
            this.hasPartialData = false;
            this.target.refreshPartFileState();
            this._notifyChange();
          }
        } finally {
          this._promiseRemovePartialData = null;
        }
      })();
    }

    return this._promiseRemovePartialData;
  },

  /**
   * Returns true if the download source is the same as the target file.
   */
  downloadingToSameFile() {
    if (!this.source.url || !this.source.url.startsWith("file:")) {
      return false;
    }

    try {
      let sourceUri = lazy.NetUtil.newURI(this.source.url);
      let targetUri = lazy.NetUtil.newURI(
        new lazy.FileUtils.File(this.target.path)
      );
      return sourceUri.equals(targetUri);
    } catch (ex) {
      return false;
    }
  },

  /**
   * This deferred object contains a promise that is resolved as soon as this
   * download finishes successfully, and is never rejected.  This property is
   * initialized when the download is created, and never changes.
   */
  _deferSucceeded: null,

  /**
   * Returns a promise that is resolved as soon as this download finishes
   * successfully, even if the download was stopped and restarted meanwhile.
   *
   * You can use this property for scheduling download completion actions in the
   * current session, for downloads that are controlled interactively.  If the
   * download is not controlled interactively, you should use the promise
   * returned by the "start" method instead, to check for success or failure.
   *
   * @returns {Promise<void>}
   *   Resolves when the download has finished successfully.
   * @rejects Never.
   */
  whenSucceeded: function D_whenSucceeded() {
    return this._deferSucceeded.promise;
  },

  /**
   * Updates the state of a finished, failed, or canceled download based on the
   * current state in the file system.  If the download is in progress or it has
   * been finalized, this method has no effect, and it returns a resolved
   * promise.
   *
   * This allows the properties of the download to be updated in case the user
   * moved or deleted the target file or its associated ".part" file.
   *
   * @returns {Promise<void>}
   *   Resolves when the operation has completed.
   * @rejects Never.
   */
  refresh() {
    return (async () => {
      if (!this.stopped || this._finalized) {
        return;
      }

      if (this.succeeded) {
        let oldExists = this.target.exists;
        let oldSize = this.target.size;
        await this.target.refresh();
        if (oldExists != this.target.exists || oldSize != this.target.size) {
          this._notifyChange();
        }
        return;
      }

      // Update the current progress from disk if we retained partial data.
      if (
        (this.hasPartialData || this.hasBlockedData) &&
        this.target.partFilePath
      ) {
        try {
          let stat = await IOUtils.stat(this.target.partFilePath);

          // Ignore the result if the state has changed meanwhile.
          if (!this.stopped || this._finalized) {
            return;
          }

          // Update the bytes transferred and the related progress properties.
          this.currentBytes = stat.size;
          if (this.totalBytes > 0) {
            this.hasProgress = true;
            this.progress = Math.floor(
              (this.currentBytes / this.totalBytes) * 100
            );
          }
        } catch (ex) {
          if (ex.name != "NotFoundError") {
            throw ex;
          }
          // Ignore the result if the state has changed meanwhile.
          if (!this.stopped || this._finalized) {
            return;
          }
          // In case we've blocked the Download becasue its
          // insecure, we should not set hasBlockedData to
          // false as its required to show the Unblock option.
          if (
            this.error.reputationCheckVerdict ==
            DownloadError.BLOCK_VERDICT_INSECURE
          ) {
            return;
          }

          this.hasBlockedData = false;
          this.hasPartialData = false;
        }

        this._notifyChange();
      }
    })().catch(console.error);
  },

  /**
   * True if the "finalize" method has been called.  This prevents the download
   * from starting again after having been stopped.
   */
  _finalized: false,

  /**
   * True if the "finalize" has been called and fully finished it's execution.
   */
  _finalizeExecuted: false,

  /**
   * Ensures that the download is stopped, and optionally removes any partial
   * data kept as part of a canceled or failed download.  After this method has
   * been called, the download cannot be started again.
   *
   * This method should be used in place of "cancel" and removePartialData while
   * shutting down or disposing of the download object, to prevent other callers
   * from interfering with the operation.  This is required because cancellation
   * and other operations are asynchronous.
   *
   * @param aRemovePartialData
   *        Whether any partially downloaded data should be removed after the
   *        download has been stopped.
   *
   * @returns {Promise<void>}
   *   Resolves when the operation has finished successfully.
   * @rejects JavaScript exception if an error occurred while removing the
   *          partially downloaded data.
   */
  finalize(aRemovePartialData) {
    // Prevents the download from starting again after having been stopped.
    this._finalized = true;
    let promise;

    if (aRemovePartialData) {
      // Cancel the download, in case it is currently in progress, then remove
      // any partially downloaded data.  The removal operation waits for
      // cancellation to be completed before resolving the promise it returns.
      this.cancel();
      promise = this.removePartialData();
    } else {
      // Just cancel the download, in case it is currently in progress.
      promise = this.cancel();
    }
    promise.then(() => {
      // At this point, either removing data / just cancelling the download should be done.
      this._finalizeExecuted = true;
    });

    return promise;
  },

  /**
   * Deletes all file data associated with a download, preserving the download
   * object itself and updating it for download views.
   */
  async manuallyRemoveData() {
    let { path } = this.target;
    if (this.succeeded) {
      // Temp files are made "read-only" by DownloadIntegration.downloadDone, so
      // reset the permission bits to read/write. This won't be necessary after
      // bug 1733587 since Downloads won't ever be temporary.
      await IOUtils.setPermissions(path, 0o660);
      await IOUtils.remove(path, { ignoreAbsent: true });
    }
    this.deleted = true;
    await this.cancel();
    await this.removePartialData();
    // We need to guarantee that the UI is refreshed irrespective of what state
    // the download is in when this is called, to ensure the download doesn't
    // wind up stuck displaying as if it exists when it actually doesn't. And
    // that means updating this.target.partFileExists no matter what.
    await this.target.refreshPartFileState();
    await this.refresh();
    // The above methods will sometimes call _notifyChange, but not always. It
    // depends on whether the download is `succeeded`, `stopped`, `canceled`,
    // etc. Since this method needs to update the UI and can be invoked on any
    // download as long as its target has some file on the system, we need to
    // call _notifyChange no matter what state the download is in.
    this._notifyChange();
  },

  /**
   * Indicates the time of the last progress notification, expressed as the
   * number of milliseconds since January 1, 1970, 00:00:00 UTC.  This is zero
   * until some bytes have actually been transferred.
   */
  _lastProgressTimeMs: 0,

  /**
   * Updates progress notifications based on the number of bytes transferred.
   *
   * The number of bytes transferred is not updated unless enough time passed
   * since this function was last called.  This limits the computation load, in
   * particular when the listeners update the user interface in response.
   *
   * @param aCurrentBytes
   *        Number of bytes transferred until now.
   * @param aTotalBytes
   *        Total number of bytes to be transferred, or -1 if unknown.
   * @param [aHasPartialData]
   *        Indicates whether the partially downloaded data can be used when
   *        restarting the download if it fails or is canceled.
   */
  _setBytes: function D_setBytes(
    aCurrentBytes,
    aTotalBytes,
    aHasPartialData = false
  ) {
    let changeMade = this.hasPartialData != aHasPartialData;
    this.hasPartialData = aHasPartialData;

    // Unless aTotalBytes is -1, we can report partial download progress.  In
    // this case, notify when the related properties changed since last time.
    if (
      aTotalBytes != -1 &&
      (!this.hasProgress || this.totalBytes != aTotalBytes)
    ) {
      this.hasProgress = true;
      this.totalBytes = aTotalBytes;
      changeMade = true;
    }

    // Updating the progress and computing the speed require that enough time
    // passed since the last update, or that we haven't started throttling yet.
    let currentTimeMs = Date.now();
    let intervalMs = currentTimeMs - this._lastProgressTimeMs;
    if (intervalMs >= kProgressUpdateIntervalMs) {
      // Don't compute the speed unless we started throttling notifications.
      if (this._lastProgressTimeMs != 0) {
        // Calculate the speed in bytes per second.
        let rawSpeed =
          ((aCurrentBytes - this.currentBytes) / intervalMs) * 1000;
        if (this.speed == 0) {
          // When the previous speed is exactly zero instead of a fractional
          // number, this can be considered the first element of the series.
          this.speed = rawSpeed;
        } else {
          // Apply exponential smoothing, with a smoothing factor of 0.1.
          this.speed = rawSpeed * 0.1 + this.speed * 0.9;
        }
      }

      // Start throttling notifications only when we have actually received some
      // bytes for the first time.  The timing of the first part of the download
      // is not reliable, due to possible latency in the initial notifications.
      // This also allows automated tests to receive and verify the number of
      // bytes initially transferred.
      if (aCurrentBytes > 0) {
        this._lastProgressTimeMs = currentTimeMs;

        // Update the progress now that we don't need its previous value.
        this.currentBytes = aCurrentBytes;
        if (this.totalBytes > 0) {
          this.progress = Math.floor(
            (this.currentBytes / this.totalBytes) * 100
          );
        }
        changeMade = true;
      }

      if (this.hasProgress && this.target && !this.target.partFileExists) {
        this.target.refreshPartFileState();
      }
    }

    if (changeMade) {
      this._notifyChange();
    }
  },

  /**
   * Returns a static representation of the current object state.
   *
   * @return A JavaScript object that can be serialized to JSON.
   */
  toSerializable() {
    let serializable = {
      source: this.source.toSerializable(),
      target: this.target.toSerializable(),
    };

    let saver = this.saver.toSerializable();
    if (!serializable.source || !saver) {
      // If we are unable to serialize either the source or the saver,
      // we won't persist the download.
      return null;
    }

    // Simplify the representation for the most common saver type.  If the saver
    // is an object instead of a simple string, we can't simplify it because we
    // need to persist all its properties, not only "type".  This may happen for
    // savers of type "copy" as well as other types.
    if (saver !== "copy") {
      serializable.saver = saver;
    }

    if (this.error) {
      serializable.errorObj = this.error.toSerializable();
    }

    if (this.startTime) {
      serializable.startTime = this.startTime.toJSON();
    }

    // These are serialized unless they are false, null, or empty strings.
    for (let property of kPlainSerializableDownloadProperties) {
      if (this[property]) {
        serializable[property] = this[property];
      }
    }

    serializeUnknownProperties(this, serializable);

    return serializable;
  },

  /**
   * Returns a value that changes only when one of the properties of a Download
   * object that should be saved into a file also change.  This excludes
   * properties whose value doesn't usually change during the download lifetime.
   *
   * This function is used to determine whether the download should be
   * serialized after a property change notification has been received.
   *
   * @return String representing the relevant download state.
   */
  getSerializationHash() {
    // The "succeeded", "canceled", "error", and startTime properties are not
    // taken into account because they all change before the "stopped" property
    // changes, and are not altered in other cases.
    return (
      this.stopped +
      "," +
      this.totalBytes +
      "," +
      this.hasPartialData +
      "," +
      this.contentType
    );
  },
};

/**
 * Defines which properties of the Download object are serializable.
 */
const kPlainSerializableDownloadProperties = [
  "succeeded",
  "canceled",
  "deleted",
  "totalBytes",
  "hasPartialData",
  "hasBlockedData",
  "tryToKeepPartialData",
  "launcherPath",
  "launcherId",
  "launchWhenSucceeded",
  "contentType",
  "handleInternally",
  "openDownloadsListOnStart",
];

/**
 * Creates a new Download object from a serializable representation.  This
 * function is used by the createDownload method of Downloads.sys.mjs when a new
 * Download object is requested, thus some properties may refer to live objects
 * in place of their serializable representations.
 *
 * @param aSerializable
 *        An object with the following fields:
 *        {
 *          source: DownloadSource object, or its serializable representation.
 *                  See DownloadSource.fromSerializable for details.
 *          target: DownloadTarget object, or its serializable representation.
 *                  See DownloadTarget.fromSerializable for details.
 *          saver: Serializable representation of a DownloadSaver object.  See
 *                 DownloadSaver.fromSerializable for details.  If omitted,
 *                 defaults to "copy".
 *        }
 *
 * @return The newly created Download object.
 */
Download.fromSerializable = function (aSerializable) {
  let download = new Download();
  if (aSerializable.source instanceof DownloadSource) {
    download.source = aSerializable.source;
  } else {
    download.source = DownloadSource.fromSerializable(aSerializable.source);
  }
  if (aSerializable.target instanceof DownloadTarget) {
    download.target = aSerializable.target;
  } else {
    download.target = DownloadTarget.fromSerializable(aSerializable.target);
  }
  if ("saver" in aSerializable) {
    download.saver = DownloadSaver.fromSerializable(aSerializable.saver);
  } else {
    download.saver = DownloadSaver.fromSerializable("copy");
  }
  download.saver.download = download;

  if ("startTime" in aSerializable) {
    let time = aSerializable.startTime.getTime
      ? aSerializable.startTime.getTime()
      : aSerializable.startTime;
    download.startTime = new Date(time);
  }

  // If 'errorObj' is present it will take precedence over the 'error' property.
  // 'error' is a legacy property only containing message, which is insufficient
  // to represent all of the error information.
  //
  // Instead of just replacing 'error' we use a new 'errorObj' so that previous
  // versions will keep it as an unknown property.
  if ("errorObj" in aSerializable) {
    download.error = DownloadError.fromSerializable(aSerializable.errorObj);
  } else if ("error" in aSerializable) {
    download.error = aSerializable.error;
  }

  for (let property of kPlainSerializableDownloadProperties) {
    if (property in aSerializable) {
      download[property] = aSerializable[property];
    }
  }

  deserializeUnknownProperties(
    download,
    aSerializable,
    property =>
      !kPlainSerializableDownloadProperties.includes(property) &&
      property != "startTime" &&
      property != "source" &&
      property != "target" &&
      property != "error" &&
      property != "saver"
  );

  return download;
};

/**
 * Checks a batch for any running downloads, emptying the batch if none are found.
 * Returns a boolean indicating if _notifyChange() needs to be called on the
 * triggering download (true) or if _updateBatch did the work of calling
 * _notifyChange() on all of the downloads in the batch (false).
 */
Download._updateBatch = function (batch) {
  const batchArray = Array.from(batch);
  for (let download of batchArray) {
    if (!download.stopped) {
      return true;
    }
  }
  batch.clear();
  for (let download of batchArray) {
    download._batch = null;
    download._notifyChange();
  }
  return false;
};

/**
 * Represents the source of a download, for example a document or an URI.
 */
export var DownloadSource = function () {};

DownloadSource.prototype = {
  /**
   * String containing the URI for the download source.
   */
  url: null,

  /**
   * True if the body of a large data URI was stripped from url after the
   * download completed, to avoid holding a large string in memory.
   */
  isDataURICleared: false,

  /**
   * String containing the original URL for the download source.
   */
  originalUrl: null,

  /**
   * Indicates whether the download was triggered by the request
   * with `Content-Disposition` header.
   */
  triggeredByContentDispositionHeader: false,

  /**
   * Indicates whether the download originated from a private window.  This
   * determines the context of the network request that is made to retrieve the
   * resource.
   */
  isPrivate: false,

  /**
   * Represents the referrerInfo of the download source, could be null for
   * example if the download source is not HTTP.
   */
  referrerInfo: null,

  /**
   * For downloads handled by the (default) DownloadCopySaver, this function
   * can adjust the network channel before it is opened, for example to change
   * the HTTP headers or to upload a stream as POST data.
   *
   * Note: If this is defined this object will not be serializable, thus the
   *       Download object will not be persisted across sessions.
   *
   * @param aChannel
   *        The nsIChannel to be adjusted.
   *
   * @returns {Promise<void>}
   *   Resolves when the channel has been adjusted and can be opened.
   * @rejects JavaScript exception that will cause the download to fail.
   */
  adjustChannel: null,

  /**
   * For downloads handled by the (default) DownloadCopySaver, this function
   * will determine, if provided, if a download can progress or has to be
   * cancelled based on the HTTP status code of the network channel.
   *
   * Note: If this is defined this object will not be serializable, thus the
   *       Download object will not be persisted across sessions.
   *
   * @param aDownload
   *        The download asking.
   * @param aStatus
   *        The HTTP status in question
   *
   * @return {boolean} Download can progress
   */
  allowHttpStatus: null,

  /**
   * Represents the loadingPrincipal of the download source,
   * could be null, in which case the system principal is used instead.
   */
  loadingPrincipal: null,

  /**
   * Represents the cookieJarSettings of the download source, could be null if
   * the download source is not from a document.
   */
  cookieJarSettings: null,

  /**
   * Represents the authentication header of the download source, could be null if
   * the download source had no authentication header.
   */
  authHeader: null,
  /**
   * Returns a static representation of the current object state.
   *
   * @return A JavaScript object that can be serialized to JSON.
   */
  toSerializable() {
    if (this.isDataURICleared) {
      return null;
    }

    if (this.adjustChannel) {
      // If the callback was used, we can't reproduce this across sessions.
      return null;
    }

    if (this.allowHttpStatus) {
      // If the callback was used, we can't reproduce this across sessions.
      return null;
    }

    let serializable = { url: this.url };
    if (this.isPrivate) {
      serializable.isPrivate = true;
    }

    if (this.referrerInfo && isString(this.referrerInfo)) {
      serializable.referrerInfo = this.referrerInfo;
    } else if (this.referrerInfo) {
      serializable.referrerInfo = lazy.E10SUtils.serializeReferrerInfo(
        this.referrerInfo
      );
    }

    if (this.loadingPrincipal) {
      serializable.loadingPrincipal = isString(this.loadingPrincipal)
        ? this.loadingPrincipal
        : lazy.E10SUtils.serializePrincipal(this.loadingPrincipal);
    }

    if (this.cookieJarSettings) {
      serializable.cookieJarSettings = isString(this.cookieJarSettings)
        ? this.cookieJarSettings
        : lazy.E10SUtils.serializeCookieJarSettings(this.cookieJarSettings);
    }

    if (this.triggeredByContentDispositionHeader) {
      serializable.triggeredByContentDispositionHeader = true;
    }

    serializeUnknownProperties(this, serializable);

    // Simplify the representation if we don't have other details.
    if (Object.keys(serializable).length === 1) {
      // serializable's only key is "url", just return the URL as a string.
      return this.url;
    }
    return serializable;
  },
};

/**
 * Creates a new DownloadSource object from its serializable representation.
 *
 * @param aSerializable
 *        Serializable representation of a DownloadSource object.  This may be a
 *        string containing the URI for the download source, an nsIURI, or an
 *        object with the following properties:
 *        {
 *          url: String containing the URI for the download source.
 *          isPrivate: Indicates whether the download originated from a private
 *                     window.  If omitted, the download is public.
 *          triggeredByContentDispositionHeader: Indicates whether the download
 *                                               was triggered by the request
 *                                               with `Content-Disposition` header.
 *          referrerInfo: represents the referrerInfo of the download source.
 *                        Can be omitted or null for example if the download
 *                        source is not HTTP.
 *          cookieJarSettings: represents the cookieJarSettings of the download
 *                             source. Can be omitted or null if the download
 *                             source is not from a document.
 *          adjustChannel: For downloads handled by (default) DownloadCopySaver,
 *                         this function can adjust the network channel before
 *                         it is opened, for example to change the HTTP headers
 *                         or to upload a stream as POST data.  Optional.
 *          allowHttpStatus: For downloads handled by the (default)
 *                           DownloadCopySaver, this function will determine, if
 *                           provided, if a download can progress or has to be
 *                           cancelled based on the HTTP status code of the
 *                           network channel.
 *        }
 *
 * @return The newly created DownloadSource object.
 */
DownloadSource.fromSerializable = function (aSerializable) {
  let source = new DownloadSource();
  if (isString(aSerializable)) {
    // Convert String objects to primitive strings at this point.
    source.url = aSerializable.toString();
  } else if (aSerializable instanceof Ci.nsIURI) {
    source.url = aSerializable.spec;
  } else {
    // Convert String objects to primitive strings at this point.
    source.url = aSerializable.url.toString();
    for (let propName of [
      "isPrivate",
      "userContextId",
      "browsingContextId",
      "triggeredByContentDispositionHeader",
    ]) {
      if (propName in aSerializable) {
        source[propName] = aSerializable[propName];
      }
    }
    if ("originalUrl" in aSerializable) {
      source.originalUrl = aSerializable.originalUrl;
    }
    if ("referrerInfo" in aSerializable) {
      // Quick pass, pass directly nsIReferrerInfo, we don't need to serialize
      // and deserialize
      if (aSerializable.referrerInfo instanceof Ci.nsIReferrerInfo) {
        source.referrerInfo = aSerializable.referrerInfo;
      } else {
        source.referrerInfo = lazy.E10SUtils.deserializeReferrerInfo(
          aSerializable.referrerInfo
        );
      }
    }
    if ("loadingPrincipal" in aSerializable) {
      // Quick pass, pass directly nsIPrincipal, we don't need to serialize
      // and deserialize
      if (aSerializable.loadingPrincipal instanceof Ci.nsIPrincipal) {
        source.loadingPrincipal = aSerializable.loadingPrincipal;
      } else {
        source.loadingPrincipal = lazy.E10SUtils.deserializePrincipal(
          aSerializable.loadingPrincipal
        );
      }
    }
    if ("adjustChannel" in aSerializable) {
      source.adjustChannel = aSerializable.adjustChannel;
    }

    if ("allowHttpStatus" in aSerializable) {
      source.allowHttpStatus = aSerializable.allowHttpStatus;
    }

    if ("cookieJarSettings" in aSerializable) {
      if (aSerializable.cookieJarSettings instanceof Ci.nsICookieJarSettings) {
        source.cookieJarSettings = aSerializable.cookieJarSettings;
      } else {
        source.cookieJarSettings = lazy.E10SUtils.deserializeCookieJarSettings(
          aSerializable.cookieJarSettings
        );
      }
    }

    if ("authHeader" in aSerializable) {
      source.authHeader = aSerializable.authHeader;
    }

    deserializeUnknownProperties(
      source,
      aSerializable,
      property =>
        property != "url" &&
        property != "originalUrl" &&
        property != "isPrivate" &&
        property != "referrerInfo" &&
        property != "cookieJarSettings" &&
        property != "authHeader"
    );
  }

  return source;
};

/**
 * Represents the target of a download, for example a file in the global
 * downloads directory, or a file in the system temporary directory.
 */
export var DownloadTarget = function () {};

DownloadTarget.prototype = {
  /**
   * String containing the path of the target file.
   */
  path: null,

  /**
   * String containing the path of the ".part" file containing the data
   * downloaded so far, or null to disable the use of a ".part" file to keep
   * partially downloaded data.
   */
  partFilePath: null,

  /**
   * Indicates whether the target file exists.
   *
   * This is a dynamic property updated when the download finishes or when the
   * "refresh" method of the Download object is called. It can be used by the
   * front-end to reduce I/O compared to checking the target file directly.
   */
  exists: false,

  /**
   * Indicates whether the part file exists. Like `exists`, this is updated
   * dynamically to reduce I/O compared to checking the target file directly.
   */
  partFileExists: false,

  /**
   * Size in bytes of the target file, or zero if the download has not finished.
   *
   * Even if the target file does not exist anymore, this property may still
   * have a value taken from the download metadata. If the metadata has never
   * been available in this session and the size cannot be obtained from the
   * file because it has already been deleted, this property will be zero.
   *
   * For single-file downloads, this property will always match the actual file
   * size on disk, while the totalBytes property of the Download object, when
   * available, may represent the size of the encoded data instead.
   *
   * For downloads involving multiple files, like complete web pages saved to
   * disk, the meaning of this value is undefined. It currently matches the size
   * of the main file only rather than the sum of all the written data.
   *
   * This is a dynamic property updated when the download finishes or when the
   * "refresh" method of the Download object is called. It can be used by the
   * front-end to reduce I/O compared to checking the target file directly.
   */
  size: 0,

  /**
   * Sets the "exists" and "size" properties based on the actual file on disk.
   *
   * @returns {Promise<void>}
   *   Resolves when the operation has finished successfully.
   * @rejects JavaScript exception.
   */
  async refresh() {
    try {
      this.size = (await IOUtils.stat(this.path)).size;
      this.exists = true;
    } catch (ex) {
      // Report any error not caused by the file not being there. In any case,
      // the size of the download is not updated and the known value is kept.
      if (ex.name != "NotFoundError") {
        console.error(ex);
      }
      this.exists = false;
    }
    this.refreshPartFileState();
  },

  async refreshPartFileState() {
    if (!this.partFilePath) {
      this.partFileExists = false;
      return;
    }
    try {
      this.partFileExists = (await IOUtils.stat(this.partFilePath)).size > 0;
    } catch (ex) {
      if (ex.name != "NotFoundError") {
        console.error(ex);
      }
      this.partFileExists = false;
    }
  },

  /**
   * Returns a static representation of the current object state.
   *
   * @return A JavaScript object that can be serialized to JSON.
   */
  toSerializable() {
    // Simplify the representation if we don't have other details.
    if (!this.partFilePath && !this._unknownProperties) {
      return this.path;
    }

    let serializable = { path: this.path, partFilePath: this.partFilePath };
    serializeUnknownProperties(this, serializable);
    return serializable;
  },
};

/**
 * Creates a new DownloadTarget object from its serializable representation.
 *
 * @param aSerializable
 *        Serializable representation of a DownloadTarget object.  This may be a
 *        string containing the path of the target file, an nsIFile, or an
 *        object with the following properties:
 *        {
 *          path: String containing the path of the target file.
 *          partFilePath: optional string containing the part file path.
 *        }
 *
 * @return The newly created DownloadTarget object.
 */
DownloadTarget.fromSerializable = function (aSerializable) {
  let target = new DownloadTarget();
  if (isString(aSerializable)) {
    // Convert String objects to primitive strings at this point.
    target.path = aSerializable.toString();
  } else if (aSerializable instanceof Ci.nsIFile) {
    // Read the "path" property of nsIFile after checking the object type.
    target.path = aSerializable.path;
  } else {
    // Read the "path" property of the serializable DownloadTarget
    // representation, converting String objects to primitive strings.
    target.path = aSerializable.path.toString();
    if ("partFilePath" in aSerializable) {
      target.partFilePath = aSerializable.partFilePath;
    }

    deserializeUnknownProperties(
      target,
      aSerializable,
      property => property != "path" && property != "partFilePath"
    );
  }
  return target;
};

/**
 * Provides detailed information about a download failure.
 *
 * @param aProperties
 *        Object which may contain any of the following properties:
 *          {
 *            result: Result error code, defaulting to Cr.NS_ERROR_FAILURE
 *            message: String error message to be displayed in the console, or
 *                     null to use the message associated with the result code.
 *            inferCause: If true, attempts to determine if the cause of the
 *                        download is a network failure or a local file failure,
 *                        based on a set of known values of the result code.
 *                        This is useful when the error is received by a
 *                        component that handles both aspects of the download.
 *            localizedReason: If available, is a localized reason for the error
 *                             that can be directly displayed in the UI.
 *          }
 *        The properties object may also contain any of the DownloadError's
 *        because properties, which will be set accordingly in the error object.
 */
export var DownloadError = function (aProperties) {
  const NS_ERROR_MODULE_BASE_OFFSET = 0x45;
  const NS_ERROR_MODULE_NETWORK = 6;
  const NS_ERROR_MODULE_FILES = 13;

  // Set the error name used by the Error object prototype first.
  this.name = "DownloadError";
  this.result = aProperties.result || Cr.NS_ERROR_FAILURE;
  this.localizedReason = aProperties.localizedReason;
  if (aProperties.message) {
    this.message = aProperties.message;
  } else if (
    aProperties.becauseBlocked ||
    aProperties.becauseBlockedByParentalControls ||
    aProperties.becauseBlockedByReputationCheck ||
    aProperties.becauseBlockedByContentAnalysis
  ) {
    this.message = "Download blocked.";
  } else {
    let exception = new Components.Exception("", this.result);
    this.message = exception.toString();
  }
  if (aProperties.inferCause) {
    let module =
      ((this.result & 0x7fff0000) >> 16) - NS_ERROR_MODULE_BASE_OFFSET;
    this.becauseSourceFailed = module == NS_ERROR_MODULE_NETWORK;
    this.becauseTargetFailed = module == NS_ERROR_MODULE_FILES;
  } else {
    if (aProperties.becauseSourceFailed) {
      this.becauseSourceFailed = true;
    }
    if (aProperties.becauseTargetFailed) {
      this.becauseTargetFailed = true;
    }
  }

  if (aProperties.becauseBlockedByParentalControls) {
    this.becauseBlocked = true;
    this.becauseBlockedByParentalControls = true;
  } else if (aProperties.becauseBlockedByReputationCheck) {
    this.becauseBlocked = true;
    this.becauseBlockedByReputationCheck = true;
    this.reputationCheckVerdict = aProperties.reputationCheckVerdict || "";
  } else if (aProperties.becauseBlockedByContentAnalysis) {
    this.becauseBlocked = true;
    this.becauseBlockedByContentAnalysis = true;
    this.contentAnalysisCancelError = aProperties.contentAnalysisCancelError;
    this.contentAnalysisWarnRequestToken =
      aProperties.contentAnalysisWarnRequestToken;
    this.reputationCheckVerdict = aProperties.reputationCheckVerdict;
  } else if (aProperties.becauseBlocked) {
    this.becauseBlocked = true;
  }

  if (aProperties.innerException) {
    this.innerException = aProperties.innerException;
  }

  this.stack = new Error().stack;
};

/**
 * These constants are used by the reputationCheckVerdict property and indicate
 * the detailed reason why a download is blocked.
 *
 * Note: These values should not be changed because they can be serialized.
 */
DownloadError.BLOCK_VERDICT_MALWARE = "Malware";
DownloadError.BLOCK_VERDICT_POTENTIALLY_UNWANTED = "PotentiallyUnwanted";
DownloadError.BLOCK_VERDICT_INSECURE = "Insecure";
DownloadError.BLOCK_VERDICT_UNCOMMON = "Uncommon";
DownloadError.BLOCK_VERDICT_DOWNLOAD_SPAM = "DownloadSpam";

DownloadError.prototype = {
  /**
   * The result code associated with this error.
   */
  result: false,

  /**
   * Indicates an error occurred while reading from the remote location.
   */
  becauseSourceFailed: false,

  /**
   * Indicates an error occurred while writing to the local target.
   */
  becauseTargetFailed: false,

  /**
   * Indicates the download failed because it was blocked.  If the reason for
   * blocking is known, the corresponding property will be also set.
   */
  becauseBlocked: false,

  /**
   * Indicates the download was blocked because downloads are globally
   * disallowed by the Parental Controls or Family Safety features on Windows.
   */
  becauseBlockedByParentalControls: false,

  /**
   * Indicates the download was blocked because it failed the reputation check
   * and may be malware.
   */
  becauseBlockedByReputationCheck: false,

  /**
   * Indicates the download was blocked by a local content analysis tool.
   */
  becauseBlockedByContentAnalysis: false,

  /**
   * The cancelError returned by the content analysis tool, which corresponds
   * to the nsIContentAnalysisResponse.CancelError enum. May be undefined.
   */
  contentAnalysisCancelError: undefined,

  /**
   * If becauseBlockedByReputationCheck is true, indicates the detailed reason
   * why the download was blocked, according to the "BLOCK_VERDICT_" constants.
   *
   * If the download was not blocked or the reason for the block is unknown,
   * this will be an empty string.
   */
  reputationCheckVerdict: "",

  /**
   * If this DownloadError was caused by an exception this property will
   * contain the original exception. This will not be serialized when saving
   * to the store.
   */
  innerException: null,

  /**
   * Returns a static representation of the current object state.
   *
   * @return A JavaScript object that can be serialized to JSON.
   */
  toSerializable() {
    let serializable = {
      result: this.result,
      localizedReason: this.localizedReason,
      message: this.message,
      becauseSourceFailed: this.becauseSourceFailed,
      becauseTargetFailed: this.becauseTargetFailed,
      becauseBlocked: this.becauseBlocked,
      becauseBlockedByParentalControls: this.becauseBlockedByParentalControls,
      becauseBlockedByReputationCheck: this.becauseBlockedByReputationCheck,
      reputationCheckVerdict: this.reputationCheckVerdict,
    };

    serializeUnknownProperties(this, serializable);
    return serializable;
  },
};
Object.setPrototypeOf(DownloadError.prototype, Error.prototype);

/**
 * Creates a new DownloadError object from its serializable representation.
 *
 * @param aSerializable
 *        Serializable representation of a DownloadError object.
 *
 * @return The newly created DownloadError object.
 */
DownloadError.fromSerializable = function (aSerializable) {
  let e = new DownloadError(aSerializable);
  deserializeUnknownProperties(
    e,
    aSerializable,
    property =>
      property != "result" &&
      property != "message" &&
      property != "becauseSourceFailed" &&
      property != "becauseTargetFailed" &&
      property != "becauseBlocked" &&
      property != "becauseBlockedByParentalControls" &&
      property != "becauseBlockedByReputationCheck" &&
      property != "becauseBlockedByContentAnalysis" &&
      property != "reputationCheckVerdict" &&
      property != "contentAnalysisCancelError"
  );

  return e;
};

/**
 * Template for an object that actually transfers the data for the download.
 */
export var DownloadSaver = function () {};

DownloadSaver.prototype = {
  /**
   * Download object for raising notifications and reading properties.
   *
   * If the tryToKeepPartialData property of the download object is false, the
   * saver should never try to keep partially downloaded data if the download
   * fails.
   */
  download: null,

  /**
   * Executes the download.
   *
   * @param aSetProgressBytesFn
   *        This function may be called by the saver to report progress. It
   *        takes three arguments: the first is the number of bytes transferred
   *        until now, the second is the total number of bytes to be
   *        transferred (or -1 if unknown), the third indicates whether the
   *        partially downloaded data can be used when restarting the download
   *        if it fails or is canceled.
   * @param aSetPropertiesFn
   *        This function may be called by the saver to report information
   *        about new download properties discovered by the saver during the
   *        download process. It takes an object where the keys represents
   *        the names of the properties to set, and the value represents the
   *        value to set.
   *
   * @returns {Promise<void>}
   *   Resolves when the download has finished successfully.
   * @rejects JavaScript exception if the download failed.
   */
  async execute() {
    throw new Error("Not implemented.");
  },

  /**
   * Cancels the download.
   */
  cancel: function DS_cancel() {
    throw new Error("Not implemented.");
  },

  /**
   * Removes any target file placeholder and any partial data kept as part of a
   * canceled, failed, or temporarily blocked download.
   *
   * This method is never called until the promise returned by "execute" is
   * either resolved or rejected, and the "execute" method is not called again
   * until the promise returned by this method is resolved or rejected.
   *
   * @param canRemoveFinalTarget
   *        True if can remove target file regardless of it being a placeholder.
   * @returns {Promise<void>}
   *   Resolves when the operation has finished successfully.
   * @rejects Never.
   */
  async removeData() {},

  /**
   * This can be called by the saver implementation when the download is already
   * started, to add it to the browsing history.  This method has no effect if
   * the download is private.
   */
  addToHistory() {
    if (AppConstants.MOZ_PLACES) {
      lazy.DownloadHistory.addDownloadToHistory(this.download).catch(
        console.error
      );
    }
  },

  /**
   * Returns a static representation of the current object state.
   *
   * @return A JavaScript object that can be serialized to JSON.
   */
  toSerializable() {
    throw new Error("Not implemented.");
  },

  /**
   * Returns the SHA-256 hash of the downloaded file, if it exists.
   */
  getSha256Hash() {
    throw new Error("Not implemented.");
  },

  getSignatureInfo() {
    throw new Error("Not implemented.");
  },
}; // DownloadSaver

/**
 * Creates a new DownloadSaver object from its serializable representation.
 *
 * @param aSerializable
 *        Serializable representation of a DownloadSaver object.  If no initial
 *        state information for the saver object is needed, can be a string
 *        representing the class of the download operation, for example "copy".
 *
 * @return The newly created DownloadSaver object.
 */
DownloadSaver.fromSerializable = function (aSerializable) {
  let serializable = isString(aSerializable)
    ? { type: aSerializable }
    : aSerializable;
  let saver;
  switch (serializable.type) {
    case "copy":
      saver = DownloadCopySaver.fromSerializable(serializable);
      break;
    case "legacy":
      saver = DownloadLegacySaver.fromSerializable(serializable);
      break;
    default:
      throw new Error("Unrecoginzed download saver type.");
  }
  return saver;
};

/**
 * Saver object that simply copies the entire source file to the target.
 */
export var DownloadCopySaver = function () {};

DownloadCopySaver.prototype = {
  /**
   * BackgroundFileSaver object currently handling the download.
   */
  _backgroundFileSaver: null,

  /**
   * Indicates whether the "cancel" method has been called.  This is used to
   * prevent the request from starting in case the operation is canceled before
   * the BackgroundFileSaver instance has been created.
   */
  _canceled: false,

  /**
   * Save the SHA-256 hash in raw bytes of the downloaded file. This is null
   * unless BackgroundFileSaver has successfully completed saving the file.
   */
  _sha256Hash: null,

  /**
   * Save the signature info as an Array of Array of raw bytes of nsIX509Cert
   * if the file is signed. This is empty if the file is unsigned, and null
   * unless BackgroundFileSaver has successfully completed saving the file.
   */
  _signatureInfo: null,

  /**
   * Save the redirects chain as an nsIArray of nsIPrincipal.
   */
  _redirects: null,

  /**
   * True if the associated download has already been added to browsing history.
   */
  alreadyAddedToHistory: false,

  /**
   * String corresponding to the entityID property of the nsIResumableChannel
   * used to execute the download, or null if the channel was not resumable or
   * the saver was instructed not to keep partially downloaded data.
   */
  entityID: null,

  /**
   * Implements "DownloadSaver.execute".
   */
  async execute(aSetProgressBytesFn, aSetPropertiesFn) {
    this._canceled = false;

    let download = this.download;
    let targetPath = download.target.path;
    let partFilePath = download.target.partFilePath;
    let keepPartialData = download.tryToKeepPartialData;

    // Add the download to history the first time it is started in this
    // session.  If the download is restarted in a different session, a new
    // history visit will be added.  We do this just to avoid the complexity
    // of serializing this state between sessions, since adding a new visit
    // does not have any noticeable side effect.
    if (!this.alreadyAddedToHistory) {
      this.addToHistory();
      this.alreadyAddedToHistory = true;
    }

    // To reduce the chance that other downloads reuse the same final target
    // file name, we should create a placeholder as soon as possible, before
    // starting the network request.  The placeholder is also required in case
    // we are using a ".part" file instead of the final target while the
    // download is in progress.
    try {
      // If the file already exists, don't delete its contents yet.
      await IOUtils.writeUTF8(targetPath, "", { mode: "appendOrCreate" });
    } catch (ex) {
      if (!DOMException.isInstance(ex)) {
        throw ex;
      }
      // Throw a DownloadError indicating that the operation failed because of
      // the target file.  We cannot translate this into a specific result
      // code, but we preserve the original message.
      let error = new DownloadError({ message: ex.message });
      error.becauseTargetFailed = true;
      throw error;
    }

    let deferSaveComplete = Promise.withResolvers();

    if (this._canceled) {
      // Don't create the BackgroundFileSaver object if we have been
      // canceled meanwhile.
      throw new DownloadError({ message: "Saver canceled." });
    }

    // Create the object that will save the file in a background thread.
    let backgroundFileSaver = new BackgroundFileSaverStreamListener();
    backgroundFileSaver.QueryInterface(Ci.nsIStreamListener);

    try {
      // When the operation completes, reflect the status in the promise
      // returned by this download execution function.
      backgroundFileSaver.observer = {
        onTargetChange() {},
        onSaveComplete: (aSaver, aStatus) => {
          // Send notifications now that we can restart if needed.
          if (Components.isSuccessCode(aStatus)) {
            // Save the hash before freeing backgroundFileSaver.
            this._sha256Hash = aSaver.sha256Hash;
            this._signatureInfo = aSaver.signatureInfo;
            this._redirects = aSaver.redirects;
            deferSaveComplete.resolve();
          } else {
            // Infer the origin of the error from the failure code, because
            // BackgroundFileSaver does not provide more specific data.
            let properties = { result: aStatus, inferCause: true };
            deferSaveComplete.reject(new DownloadError(properties));
          }
          // Free the reference cycle, to release resources earlier.
          backgroundFileSaver.observer = null;
          this._backgroundFileSaver = null;
        },
      };

      // If we have data that we can use to resume the download from where
      // it stopped, try to use it.
      let resumeAttempted = false;
      let resumeFromBytes = 0;

      const notificationCallbacks = {
        QueryInterface: ChromeUtils.generateQI(["nsIInterfaceRequestor"]),
        getInterface: ChromeUtils.generateQI(["nsIProgressEventSink"]),
        onProgress: function DCSE_onProgress(
          aRequest,
          aProgress,
          aProgressMax
        ) {
          let currentBytes = resumeFromBytes + aProgress;
          let totalBytes =
            aProgressMax == -1 ? -1 : resumeFromBytes + aProgressMax;
          aSetProgressBytesFn(
            currentBytes,
            totalBytes,
            aProgress > 0 && partFilePath && keepPartialData
          );
        },
        onStatus() {},
      };

      const streamListener = {
        onStartRequest: function (aRequest) {
          backgroundFileSaver.onStartRequest(aRequest);

          if (aRequest instanceof Ci.nsIHttpChannel) {
            // Check if the request's response has been blocked by Windows
            // Parental Controls with an HTTP 450 error code.
            if (aRequest.responseStatus == 450) {
              // Set a flag that can be retrieved later when handling the
              // cancellation so that the proper error can be thrown.
              this.download._blockedByParentalControls = true;
              aRequest.cancel(Cr.NS_BINDING_ABORTED);
              return;
            }

            // Check back with the initiator if we should allow a certain
            // HTTP code. By default, we'll just save error pages too,
            // however a consumer down the line, such as the WebExtensions
            // downloads API might want to handle this differently.
            if (
              download.source.allowHttpStatus &&
              !download.source.allowHttpStatus(
                download,
                aRequest.responseStatus
              )
            ) {
              aRequest.cancel(Cr.NS_BINDING_ABORTED);
              return;
            }
          }

          if (aRequest instanceof Ci.nsIChannel) {
            aSetPropertiesFn({ contentType: aRequest.contentType });

            // Ensure we report the value of "Content-Length", if available,
            // even if the download doesn't generate any progress events
            // later.
            if (aRequest.contentLength >= 0) {
              aSetProgressBytesFn(0, aRequest.contentLength);
            }
          }

          // If the URL we are downloading from includes a file extension
          // that matches the "Content-Encoding" header, for example ".gz"
          // with a "gzip" encoding, we should save the file in its encoded
          // form.  In all other cases, we decode the body while saving.
          if (
            aRequest instanceof Ci.nsIEncodedChannel &&
            aRequest.contentEncodings
          ) {
            let uri = aRequest.URI;
            if (uri instanceof Ci.nsIURL && uri.fileExtension) {
              // Only the first, outermost encoding is considered.
              let encoding = aRequest.contentEncodings.getNext();
              if (encoding) {
                aRequest.applyConversion =
                  lazy.gExternalHelperAppService.applyDecodingForExtension(
                    uri.fileExtension,
                    encoding
                  );
              }
            }
          }

          if (keepPartialData) {
            // If the source is not resumable, don't keep partial data even
            // if we were asked to try and do it.
            if (aRequest instanceof Ci.nsIResumableChannel) {
              try {
                // If reading the ID succeeds, the source is resumable.
                this.entityID = aRequest.entityID;
              } catch (ex) {
                if (
                  !(ex instanceof Components.Exception) ||
                  ex.result != Cr.NS_ERROR_NOT_RESUMABLE
                ) {
                  throw ex;
                }
                keepPartialData = false;
              }
            } else {
              keepPartialData = false;
            }
          }

          // Enable hashing and signature verification before setting the
          // target.
          backgroundFileSaver.enableSha256();
          backgroundFileSaver.enableSignatureInfo();
          if (partFilePath) {
            // If we actually resumed a request, append to the partial data.
            if (resumeAttempted) {
              // TODO: Handle Cr.NS_ERROR_ENTITY_CHANGED
              backgroundFileSaver.enableAppend();
            }

            // Use a part file, determining if we should keep it on failure.
            backgroundFileSaver.setTarget(
              new lazy.FileUtils.File(partFilePath),
              keepPartialData
            );
          } else {
            // Set the final target file, and delete it on failure.
            backgroundFileSaver.setTarget(
              new lazy.FileUtils.File(targetPath),
              false
            );
          }
        }.bind(this),

        onStopRequest(aRequest, aStatusCode) {
          try {
            backgroundFileSaver.onStopRequest(aRequest, aStatusCode);
          } finally {
            // If the data transfer completed successfully, indicate to the
            // background file saver that the operation can finish.  If the
            // data transfer failed, the saver has been already stopped.
            if (Components.isSuccessCode(aStatusCode)) {
              backgroundFileSaver.finish(Cr.NS_OK);
            }
          }
        },

        onDataAvailable: (aRequest, aInputStream, aOffset, aCount) => {
          // Check if the download have been canceled in the mean time,
          // and close the channel and return earlier, BackgroundFileSaver
          // methods shouldn't be called anymore after `finish` was called
          // on download cancellation.
          if (this._canceled) {
            aRequest.cancel(Cr.NS_BINDING_ABORTED);
            return;
          }
          backgroundFileSaver.onDataAvailable(
            aRequest,
            aInputStream,
            aOffset,
            aCount
          );
        },
      };

      // Wrap the channel creation, to prevent the listener code from
      // accidentally using the wrong channel.
      // The channel that is created here is not necessarily the same channel
      // that will eventually perform the actual download.
      // When a HTTP redirect happens, the http backend will create a new
      // channel, this initial channel will be abandoned, and its properties
      // will either return incorrect data, or worse, will throw exceptions
      // upon access.
      const open = async () => {
        // Create a channel from the source, and listen to progress
        // notifications.
        let channel;
        if (download.source.loadingPrincipal) {
          channel = lazy.NetUtil.newChannel({
            uri: download.source.url,
            contentPolicyType: Ci.nsIContentPolicy.TYPE_SAVEAS_DOWNLOAD,
            loadingPrincipal: download.source.loadingPrincipal,
            // triggeringPrincipal must be the system principal to prevent the
            // request from being mistaken as a third-party request.
            triggeringPrincipal:
              Services.scriptSecurityManager.getSystemPrincipal(),
            securityFlags:
              Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
          });
        } else {
          channel = lazy.NetUtil.newChannel({
            uri: download.source.url,
            contentPolicyType: Ci.nsIContentPolicy.TYPE_SAVEAS_DOWNLOAD,
            loadUsingSystemPrincipal: true,
          });
        }
        if (channel instanceof Ci.nsIPrivateBrowsingChannel) {
          channel.setPrivate(download.source.isPrivate);
        }
        if (
          channel instanceof Ci.nsIHttpChannel &&
          download.source.referrerInfo
        ) {
          channel.referrerInfo = download.source.referrerInfo;
          // Stored computed referrerInfo;
          download.source.referrerInfo = channel.referrerInfo;
        }
        if (
          channel instanceof Ci.nsIHttpChannel &&
          download.source.cookieJarSettings
        ) {
          channel.loadInfo.cookieJarSettings =
            download.source.cookieJarSettings;
        }
        if (
          channel instanceof Ci.nsIHttpChannel &&
          download.source.authHeader
        ) {
          try {
            channel.setRequestHeader(
              "Authorization",
              download.source.authHeader,
              true
            );
          } catch (e) {}
        }

        if (download.source.userContextId) {
          // Getters and setters only exist on originAttributes,
          // so it has to be cloned, changed, and re-set
          channel.loadInfo.originAttributes = {
            ...channel.loadInfo.originAttributes,
            userContextId: download.source.userContextId,
          };
        }

        // This makes the channel be corretly throttled during page loads
        // and also prevents its caching.
        if (channel instanceof Ci.nsIHttpChannelInternal) {
          channel.channelIsForDownload = true;

          // Include cookies even if cookieBehavior is BEHAVIOR_REJECT_FOREIGN.
          channel.forceAllowThirdPartyCookie = true;
        }

        if (
          channel instanceof Ci.nsIResumableChannel &&
          this.entityID &&
          partFilePath &&
          keepPartialData
        ) {
          try {
            let stat = await IOUtils.stat(partFilePath);
            channel.resumeAt(stat.size, this.entityID);
            resumeAttempted = true;
            resumeFromBytes = stat.size;
          } catch (ex) {
            if (ex.name != "NotFoundError") {
              throw ex;
            }
          }
        }

        channel.notificationCallbacks = notificationCallbacks;

        // If the callback was set, handle it now before opening the channel.
        if (download.source.adjustChannel) {
          await download.source.adjustChannel(channel);
        }
        channel.asyncOpen(streamListener);
      };

      // Kick off the download, creating and opening the channel.
      await open();

      // We should check if we have been canceled in the meantime, after
      // all the previous asynchronous operations have been executed and
      // just before we set the _backgroundFileSaver property.
      if (this._canceled) {
        throw new DownloadError({ message: "Saver canceled." });
      }

      // If the operation succeeded, store the object to allow cancellation.
      this._backgroundFileSaver = backgroundFileSaver;
    } catch (ex) {
      // In case an error occurs while setting up the chain of objects for
      // the download, ensure that we release the resources of the saver.
      backgroundFileSaver.finish(Cr.NS_ERROR_FAILURE);
      // Since we're not going to handle deferSaveComplete.promise below,
      // we need to make sure that the rejection is handled.
      deferSaveComplete.promise.catch(() => {});
      throw ex;
    }

    // We will wait on this promise in case no error occurred while setting
    // up the chain of objects for the download.
    await deferSaveComplete.promise;

    await this._checkReputationAndMove(aSetPropertiesFn);
  },

  /**
   * Perform the reputation check and cleanup the downloaded data if required.
   * If the download passes the reputation check and is using a part file we
   * will move it to the target path since reputation checking is the final
   * step in the saver.
   *
   * @param aSetPropertiesFn
   *        Function provided to the "execute" method.
   *
   * @returns {Promise<void>}
   *   Resolves when the reputation check and cleanup is complete.
   * @rejects DownloadError if the download should be blocked.
   */
  async _checkReputationAndMove(aSetPropertiesFn) {
    const REPUTATION_CHECK = 0;
    const CONTENT_ANALYSIS_CHECK = 1;
    /**
     * Maps nsIApplicationReputationService verdicts with the DownloadError ones.
     */
    const kVerdictMap = {
      [Ci.nsIApplicationReputationService.VERDICT_DANGEROUS]:
        DownloadError.BLOCK_VERDICT_MALWARE,
      [Ci.nsIApplicationReputationService.VERDICT_UNCOMMON]:
        DownloadError.BLOCK_VERDICT_UNCOMMON,
      [Ci.nsIApplicationReputationService.VERDICT_POTENTIALLY_UNWANTED]:
        DownloadError.BLOCK_VERDICT_POTENTIALLY_UNWANTED,
      [Ci.nsIApplicationReputationService.VERDICT_DANGEROUS_HOST]:
        DownloadError.BLOCK_VERDICT_MALWARE,
    };

    let checkContentAnalysis = download => {
      // Start an asynchronous content analysis check.
      return lazy.DownloadIntegration.shouldBlockForContentAnalysis(
        download
      ).then(result => {
        result.check = CONTENT_ANALYSIS_CHECK;
        return result;
      });
    };

    let checkReputation = download => {
      // Start an asynchronous reputation check.
      return lazy.DownloadIntegration.shouldBlockForReputationCheck(
        download
      ).then(result => {
        result.check = REPUTATION_CHECK;
        return result;
      });
    };

    let hasMostRestrictiveResult = ([
      reputationResult,
      contentAnalysisResult,
    ]) => {
      // Verdicts are sorted from least-to-most restrictive.  However, a result that
      // shouldBlock is always more restrictive than one that does not.  Since
      // reputation allows shouldBlock to be overridden by prefs but content
      // analysis does not, we need to be careful of that.
      if (reputationResult.shouldBlock && !contentAnalysisResult.shouldBlock) {
        return reputationResult;
      }
      if (contentAnalysisResult.shouldBlock) {
        return contentAnalysisResult;
      }
      // Verdicts are in a pre-defined order (see nsIApplicationReputationService),
      // so find the most restrictive one.
      const verdictToRestrictiveness = {
        [Ci.nsIApplicationReputationService.VERDICT_SAFE]: 0,
        [Ci.nsIApplicationReputationService.VERDICT_POTENTIALLY_UNWANTED]: 1,
        [Ci.nsIApplicationReputationService.VERDICT_UNCOMMON]: 2,
        [Ci.nsIApplicationReputationService.VERDICT_DANGEROUS_HOST]: 3,
        [Ci.nsIApplicationReputationService.VERDICT_DANGEROUS]: 4,
      };
      return verdictToRestrictiveness[reputationResult.verdict] >
        verdictToRestrictiveness[contentAnalysisResult.verdict]
        ? reputationResult
        : contentAnalysisResult;
    };

    let download = this.download;
    let targetPath = this.download.target.path;
    let partFilePath = this.download.target.partFilePath;

    let reputationPromise = checkReputation(download);
    let caPromise = checkContentAnalysis(download);

    let permissionResult = await Promise.all([
      reputationPromise,
      caPromise,
    ]).then(hasMostRestrictiveResult);

    let downloadErrorVerdict = kVerdictMap[permissionResult.verdict] || "";
    permissionResult.verdict = downloadErrorVerdict;
    if (permissionResult.shouldBlock) {
      if (permissionResult.check === REPUTATION_CHECK) {
        Glean.downloads.userActionOnBlockedDownload[
          downloadErrorVerdict
        ].accumulateSingleSample(0);
      }

      let newProperties = { progress: 100, hasPartialData: false };

      // We will remove the potentially dangerous file if instructed by
      // DownloadIntegration. We will always remove the file when the
      // download did not use a partial file path, meaning it
      // currently has its final filename, or if it was blocked by
      // content analysis.
      let neverRemoveData = false;
      let alwaysRemoveData = false;
      if (permissionResult.check === CONTENT_ANALYSIS_CHECK) {
        if (downloadErrorVerdict === DownloadError.BLOCK_VERDICT_MALWARE) {
          alwaysRemoveData = true;
        } else {
          neverRemoveData = true;
        }
      }
      let removeData =
        !neverRemoveData &&
        (alwaysRemoveData ||
          !lazy.DownloadIntegration.shouldKeepBlockedData() ||
          !partFilePath);
      if (removeData) {
        await this.removeData(!partFilePath);
      } else {
        newProperties.hasBlockedData = true;
      }

      aSetPropertiesFn(newProperties);

      if (permissionResult.check == REPUTATION_CHECK) {
        throw new DownloadError({
          becauseBlockedByReputationCheck: true,
          reputationCheckVerdict: downloadErrorVerdict,
        });
      } else {
        throw new DownloadError({
          becauseBlockedByContentAnalysis: true,
          reputationCheckVerdict: downloadErrorVerdict,
          contentAnalysisCancelError:
            permissionResult.contentAnalysisCancelError,
          contentAnalysisWarnRequestToken:
            permissionResult.contentAnalysisWarnRequestToken,
        });
      }
    }

    if (partFilePath) {
      try {
        await IOUtils.move(partFilePath, targetPath);
      } catch (e) {
        if (e.name === "NotAllowedError") {
          // In case we cannot write to the target file
          // retry with a new unique name
          let uniquePath = lazy.DownloadPaths.createNiceUniqueFile(
            new lazy.FileUtils.File(targetPath)
          ).path;
          await IOUtils.move(partFilePath, uniquePath);
          this.download.target.path = uniquePath;
        } else {
          throw e;
        }
      }
    }
  },

  /**
   * Implements "DownloadSaver.cancel".
   */
  cancel: function DCS_cancel() {
    this._canceled = true;
    if (this._backgroundFileSaver) {
      this._backgroundFileSaver.finish(Cr.NS_ERROR_FAILURE);
      this._backgroundFileSaver = null;
    }
  },

  /**
   * Implements "DownloadSaver.removeData".
   */
  async removeData(canRemoveFinalTarget = false) {
    // Defined inline so removeData can be shared with DownloadLegacySaver.
    async function _tryToRemoveFile(path) {
      try {
        await IOUtils.remove(path);
      } catch (ex) {
        // On Windows we may get an access denied error instead of a no such
        // file error if the file existed before, and was recently deleted. This
        // is likely to happen when the component that executed the download has
        // just deleted the target file itself.
        if (!["NotFoundError", "NotAllowedError"].includes(ex.name)) {
          console.error(ex);
        }
      }
    }

    if (this.download.target.partFilePath) {
      await _tryToRemoveFile(this.download.target.partFilePath);
    }

    if (this.download.target.path) {
      if (
        canRemoveFinalTarget ||
        (await isPlaceholder(this.download.target.path))
      ) {
        await _tryToRemoveFile(this.download.target.path);
      }
      this.download.target.exists = false;
      this.download.target.size = 0;
    }
  },

  /**
   * Implements "DownloadSaver.toSerializable".
   */
  toSerializable() {
    // Simplify the representation if we don't have other details.
    if (!this.entityID && !this._unknownProperties) {
      return "copy";
    }

    let serializable = { type: "copy", entityID: this.entityID };
    serializeUnknownProperties(this, serializable);
    return serializable;
  },

  /**
   * Implements "DownloadSaver.getSha256Hash"
   */
  getSha256Hash() {
    return this._sha256Hash;
  },

  /*
   * Implements DownloadSaver.getSignatureInfo.
   */
  getSignatureInfo() {
    return this._signatureInfo;
  },

  /*
   * Implements DownloadSaver.getRedirects.
   */
  getRedirects() {
    return this._redirects;
  },
};
Object.setPrototypeOf(DownloadCopySaver.prototype, DownloadSaver.prototype);

/**
 * Creates a new DownloadCopySaver object, with its initial state derived from
 * its serializable representation.
 *
 * @param aSerializable
 *        Serializable representation of a DownloadCopySaver object.
 *
 * @return The newly created DownloadCopySaver object.
 */
DownloadCopySaver.fromSerializable = function (aSerializable) {
  let saver = new DownloadCopySaver();
  if ("entityID" in aSerializable) {
    saver.entityID = aSerializable.entityID;
  }

  deserializeUnknownProperties(
    saver,
    aSerializable,
    property => property != "entityID" && property != "type"
  );

  return saver;
};

/**
 * Saver object that integrates with the legacy nsITransfer interface.
 *
 * For more background on the process, see the DownloadLegacyTransfer object.
 */
export var DownloadLegacySaver = function () {
  this.deferExecuted = Promise.withResolvers();
  this.deferCanceled = Promise.withResolvers();
};

DownloadLegacySaver.prototype = {
  /**
   * Save the SHA-256 hash in raw bytes of the downloaded file. This may be
   * null when nsExternalHelperAppService (and thus BackgroundFileSaver) is not
   * invoked.
   */
  _sha256Hash: null,

  /**
   * Save the signature info as an Array of Array of raw bytes of nsIX509Cert
   * if the file is signed. This is empty if the file is unsigned, and null
   * unless BackgroundFileSaver has successfully completed saving the file.
   */
  _signatureInfo: null,

  /**
   * Save the redirect chain as an nsIArray of nsIPrincipal.
   */
  _redirects: null,

  /**
   * nsIRequest object associated to the status and progress updates we
   * received.  This object is null before we receive the first status and
   * progress update, and is also reset to null when the download is stopped.
   */
  request: null,

  /**
   * This deferred object contains a promise that is resolved as soon as this
   * download finishes successfully, and is rejected in case the download is
   * canceled or receives a failure notification through nsITransfer.
   */
  deferExecuted: null,

  /**
   * This deferred object contains a promise that is resolved if the download
   * receives a cancellation request through the "cancel" method, and is never
   * rejected.  The nsITransfer implementation will register a handler that
   * actually causes the download cancellation.
   */
  deferCanceled: null,

  /**
   * This is populated with the value of the aSetProgressBytesFn argument of the
   * "execute" method, and is null before the method is called.
   */
  setProgressBytesFn: null,

  /**
   * Called by the nsITransfer implementation while the download progresses.
   *
   * @param aCurrentBytes
   *        Number of bytes transferred until now.
   * @param aTotalBytes
   *        Total number of bytes to be transferred, or -1 if unknown.
   */
  onProgressBytes: function DLS_onProgressBytes(aCurrentBytes, aTotalBytes) {
    this.progressWasNotified = true;

    // Ignore progress notifications until we are ready to process them.
    if (!this.setProgressBytesFn) {
      // Keep the data from the last progress notification that was received.
      this.currentBytes = aCurrentBytes;
      this.totalBytes = aTotalBytes;
      return;
    }

    let hasPartFile = !!this.download.target.partFilePath;

    this.setProgressBytesFn(
      aCurrentBytes,
      aTotalBytes,
      aCurrentBytes > 0 && hasPartFile
    );
  },

  /**
   * Whether the onProgressBytes function has been called at least once.
   */
  progressWasNotified: false,

  /**
   * Called by the nsITransfer implementation when the request has started.
   *
   * @param aRequest
   *        nsIRequest associated to the status update.
   */
  onTransferStarted(aRequest) {
    // Store a reference to the request, used in some cases when handling
    // completion, and also checked during the download by unit tests.
    this.request = aRequest;

    // Store the entity ID to use for resuming if required.
    if (
      this.download.tryToKeepPartialData &&
      aRequest instanceof Ci.nsIResumableChannel
    ) {
      try {
        // If reading the ID succeeds, the source is resumable.
        this.entityID = aRequest.entityID;
      } catch (ex) {
        if (
          !(ex instanceof Components.Exception) ||
          ex.result != Cr.NS_ERROR_NOT_RESUMABLE
        ) {
          throw ex;
        }
      }
    }

    // For legacy downloads, we must update the referrerInfo at this time.
    if (aRequest instanceof Ci.nsIHttpChannel) {
      this.download.source.referrerInfo = aRequest.referrerInfo;
    }

    // Don't open the download panel when the user initiated to save a
    // link or document.
    if (
      aRequest instanceof Ci.nsIChannel &&
      aRequest.loadInfo.isUserTriggeredSave
    ) {
      this.download.openDownloadsListOnStart = false;
    }

    this.addToHistory();
  },

  /**
   * Called by the nsITransfer implementation when the request has finished.
   *
   * @param {nsresult} status
   *        Status code received by the nsITransfer implementation.
   * @param {string} [localizedReason]
   *        Optional localized error message associated with a failure
   */
  onTransferFinished(status, localizedReason) {
    if (Components.isSuccessCode(status)) {
      this.deferExecuted.resolve();
    } else {
      // Infer the origin of the error from the failure code, because more
      // specific data is not available through the nsITransfer implementation.
      let properties = {
        result: status,
        inferCause: true,
        localizedReason,
      };
      this.deferExecuted.reject(new DownloadError(properties));
    }
  },

  /**
   * When the first execution of the download finished, it can be restarted by
   * using a DownloadCopySaver object instead of the original legacy component
   * that executed the download.
   */
  firstExecutionFinished: false,

  /**
   * In case the download is restarted after the first execution finished, this
   * property contains a reference to the DownloadCopySaver that is executing
   * the new download attempt.
   */
  copySaver: null,

  /**
   * String corresponding to the entityID property of the nsIResumableChannel
   * used to execute the download, or null if the channel was not resumable or
   * the saver was instructed not to keep partially downloaded data.
   */
  entityID: null,

  /**
   * Implements "DownloadSaver.execute".
   */
  async execute(aSetProgressBytesFn, aSetPropertiesFn) {
    // Check if this is not the first execution of the download.  The Download
    // object guarantees that this function is not re-entered during execution.
    if (this.firstExecutionFinished) {
      if (!this.copySaver) {
        this.copySaver = new DownloadCopySaver();
        this.copySaver.download = this.download;
        this.copySaver.entityID = this.entityID;
        this.copySaver.alreadyAddedToHistory = true;
      }
      await this.copySaver.execute.apply(this.copySaver, arguments);
      return;
    }

    this.setProgressBytesFn = aSetProgressBytesFn;
    if (this.progressWasNotified) {
      this.onProgressBytes(this.currentBytes, this.totalBytes);
    }

    try {
      // Wait for the component that executes the download to finish.
      await this.deferExecuted.promise;

      // At this point, the "request" property has been populated.  Ensure we
      // report the value of "Content-Length", if available, even if the
      // download didn't generate any progress events.
      if (
        !this.progressWasNotified &&
        this.request instanceof Ci.nsIChannel &&
        this.request.contentLength >= 0
      ) {
        aSetProgressBytesFn(0, this.request.contentLength);
      }

      // If the component executing the download provides the path of a
      // ".part" file, it means that it expects the listener to move the file
      // to its final target path when the download succeeds.  In this case,
      // an empty ".part" file is created even if no data was received from
      // the source.
      //
      // When no ".part" file path is provided the download implementation may
      // not have created the target file (if no data was received from the
      // source).  In this case, ensure that an empty file is created as
      // expected.
      if (!this.download.target.partFilePath) {
        try {
          // This atomic operation is more efficient than an existence check.
          await IOUtils.writeUTF8(this.download.target.path, "", {
            mode: "create",
          });
        } catch (ex) {
          if (
            !DOMException.isInstance(ex) ||
            ex.name !== "NoModificationAllowedError"
          ) {
            throw ex;
          }
        }
      }

      await this._checkReputationAndMove(aSetPropertiesFn);
    } catch (ex) {
      // In case the operation failed, ensure we stop downloading data.  Since
      // we never re-enter this function, deferCanceled is always available.
      this.deferCanceled.resolve();
      throw ex;
    } finally {
      // We don't need the reference to the request anymore.  We must also set
      // deferCanceled to null in order to free any indirect references it
      // may hold to the request.
      this.request = null;
      this.deferCanceled = null;
      // Allow the download to restart through a DownloadCopySaver.
      this.firstExecutionFinished = true;
    }
  },

  _checkReputationAndMove() {
    return DownloadCopySaver.prototype._checkReputationAndMove.apply(
      this,
      arguments
    );
  },

  /**
   * Implements "DownloadSaver.cancel".
   */
  cancel: function DLS_cancel() {
    // We may be using a DownloadCopySaver to handle resuming.
    if (this.copySaver) {
      return this.copySaver.cancel.apply(this.copySaver, arguments);
    }

    // If the download hasn't stopped already, resolve deferCanceled so that the
    // operation is canceled as soon as a cancellation handler is registered.
    // Note that the handler might not have been registered yet.
    if (this.deferCanceled) {
      this.deferCanceled.resolve();
    }
  },

  /**
   * Implements "DownloadSaver.removeData".
   */
  removeData(canRemoveFinalTarget) {
    // DownloadCopySaver and DownloadLegacySaver use the same logic for removing
    // partially downloaded data, though this implementation isn't shared by
    // other saver types, thus it isn't found on their shared prototype.
    return DownloadCopySaver.prototype.removeData.call(
      this,
      canRemoveFinalTarget
    );
  },

  /**
   * Implements "DownloadSaver.toSerializable".
   */
  toSerializable() {
    // This object depends on legacy components that are created externally,
    // thus it cannot be rebuilt during deserialization.  To support resuming
    // across different browser sessions, this object is transformed into a
    // DownloadCopySaver for the purpose of serialization.
    return DownloadCopySaver.prototype.toSerializable.call(this);
  },

  /**
   * Implements "DownloadSaver.getSha256Hash".
   */
  getSha256Hash() {
    if (this.copySaver) {
      return this.copySaver.getSha256Hash();
    }
    return this._sha256Hash;
  },

  /**
   * Called by the nsITransfer implementation when the hash is available.
   */
  setSha256Hash(hash) {
    this._sha256Hash = hash;
  },

  /**
   * Implements "DownloadSaver.getSignatureInfo".
   */
  getSignatureInfo() {
    if (this.copySaver) {
      return this.copySaver.getSignatureInfo();
    }
    return this._signatureInfo;
  },

  /**
   * Called by the nsITransfer implementation when the hash is available.
   */
  setSignatureInfo(signatureInfo) {
    this._signatureInfo = signatureInfo;
  },

  /**
   * Implements "DownloadSaver.getRedirects".
   */
  getRedirects() {
    if (this.copySaver) {
      return this.copySaver.getRedirects();
    }
    return this._redirects;
  },

  /**
   * Called by the nsITransfer implementation when the redirect chain is
   * available.
   */
  setRedirects(redirects) {
    this._redirects = redirects;
  },
};
Object.setPrototypeOf(DownloadLegacySaver.prototype, DownloadSaver.prototype);

/**
 * Returns a new DownloadLegacySaver object.  This saver type has a
 * deserializable form only when creating a new object in memory, because it
 * cannot be serialized to disk.
 */
DownloadLegacySaver.fromSerializable = function () {
  return new DownloadLegacySaver();
};
