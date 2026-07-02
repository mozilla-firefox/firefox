/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This component implements the XPCOM interfaces required for integration with
 * the legacy download components.
 *
 * New code is expected to use the "Downloads.sys.mjs" module directly, without
 * going through the interfaces implemented in this XPCOM component.  These
 * interfaces are only maintained for backwards compatibility with components
 * that still work synchronously on the main thread.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DownloadError: "resource://gre/modules/DownloadCore.sys.mjs",
  DownloadIntegration: "resource://gre/modules/DownloadIntegration.sys.mjs",
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
});

/**
 * nsITransfer implementation that provides a bridge to a Download object.
 *
 * Legacy downloads work differently than the JavaScript implementation.  In the
 * latter, the caller only provides the properties for the Download object and
 * the entire process is handled by the "start" method.  In the legacy
 * implementation, the caller must create a separate object to execute the
 * download, and then make the download visible to the user by hooking it up to
 * an nsITransfer instance.
 *
 * Since nsITransfer instances may be created before the download system is
 * initialized, and initialization as well as other operations are asynchronous,
 * this implementation is able to delay all progress and status notifications it
 * receives until the associated Download object is finally created.
 *
 * Conversely, the DownloadLegacySaver object can also receive execution and
 * cancellation requests asynchronously, before or after it is connected to
 * this nsITransfer instance.  For that reason, those requests are communicated
 * in a potentially deferred way, using promise objects.
 *
 * The component that executes the download implements nsICancelable to receive
 * cancellation requests, but after cancellation it cannot be reused again.
 *
 * Since the components that execute the download may be different and they
 * don't always give consistent results, this bridge takes care of enforcing the
 * expectations, for example by ensuring the target file exists when the
 * download is successful, even if the source has a size of zero bytes.
 */
export function DownloadLegacyTransfer() {
  this._promiseDownload = new Promise(r => (this._resolveDownload = r));
}

DownloadLegacyTransfer.prototype = {
  classID: Components.ID("{1b4c85df-cbdd-4bb6-b04e-613caece083c}"),

  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsIWebProgressListener2",
    "nsITransfer",
  ]),

  // nsIWebProgressListener
  onStateChange: function DLT_onStateChange(
    aWebProgress,
    aRequest,
    aStateFlags,
    aStatus
  ) {
    if (!Components.isSuccessCode(aStatus)) {
      this._componentFailed = true;
    }

    if (
      aStateFlags & Ci.nsIWebProgressListener.STATE_START &&
      aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK
    ) {
      let blockedByParentalControls = false;
      // If it is a failed download, aRequest.responseStatus doesn't exist.
      // (missing file on the server, network failure to download)
      try {
        // If the request's response has been blocked by Windows Parental Controls
        // with an HTTP 450 error code, we must cancel the request synchronously.
        blockedByParentalControls =
          aRequest instanceof Ci.nsIHttpChannel &&
          aRequest.responseStatus == 450;
      } catch (e) {
        if (e.result == Cr.NS_ERROR_NOT_AVAILABLE) {
          aRequest.cancel(Cr.NS_BINDING_ABORTED);
        }
      }

      if (blockedByParentalControls) {
        aRequest.cancel(Cr.NS_BINDING_ABORTED);
      }

      // The main request has just started.  Wait for the associated Download
      // object to be available before notifying.
      this._promiseDownload
        .then(download => {
          download.source.triggeredByContentDispositionHeader = false;
          if (aRequest instanceof Ci.nsIHttpChannel) {
            try {
              download.source.triggeredByContentDispositionHeader =
                !!aRequest.contentDispositionHeader;
            } catch (e) {}
          }

          // If the request was blocked, now that we have the download object we
          // should set a flag that can be retrieved later when handling the
          // cancellation so that the proper error can be thrown.
          if (blockedByParentalControls) {
            download._blockedByParentalControls = true;
          }

          download.saver.onTransferStarted(aRequest);

          // To handle asynchronous cancellation properly, we should hook up the
          // handler only after we have been notified that the main request
          // started.  We will wait until the main request stopped before
          // notifying that the download has been canceled.  Since the request has
          // not completed yet, deferCanceled is guaranteed to be set.
          return download.saver.deferCanceled.promise.then(() => {
            // Only cancel if the object executing the download is still running.
            if (this._cancelable && !this._componentFailed) {
              this._cancelable.cancel(Cr.NS_ERROR_ABORT);
            }
          });
        })
        .catch(console.error);
    } else if (
      aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
      aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK
    ) {
      // The last file has been received, or the download failed.  Wait for the
      // associated Download object to be available before notifying.
      this._promiseDownload
        .then(download => {
          // At this point, the hash has been set and we need to copy it to the
          // DownloadSaver.
          if (Components.isSuccessCode(aStatus)) {
            download.saver.setSha256Hash(this._sha256Hash);
            download.saver.setSignatureInfo(this._signatureInfo);
            download.saver.setRedirects(this._redirects);
          }
          download.saver.onTransferFinished(aStatus);
        })
        .catch(console.error);

      // Release the reference to the component executing the download.
      this._cancelable = null;
    }
  },

  // nsIWebProgressListener
  onProgressChange: function DLT_onProgressChange(
    aWebProgress,
    aRequest,
    aCurSelfProgress,
    aMaxSelfProgress,
    aCurTotalProgress,
    aMaxTotalProgress
  ) {
    this.onProgressChange64(
      aWebProgress,
      aRequest,
      aCurSelfProgress,
      aMaxSelfProgress,
      aCurTotalProgress,
      aMaxTotalProgress
    );
  },

  onLocationChange() {},

  // nsIWebProgressListener
  onStatusChange(webProgress, request, status, message) {
    // The status change may optionally be received in addition to the state
    // change, but if no network request actually started, it is possible that
    // we only receive a status change with an error status code.
    if (!Components.isSuccessCode(status)) {
      this._componentFailed = true;

      // Wait for the associated Download object to be available.
      this._promiseDownload
        .then(download => {
          download.saver.onTransferFinished(status, message);
        })
        .catch(console.error);
    }
  },

  onSecurityChange() {},

  onContentBlockingEvent() {},

  // nsIWebProgressListener2
  onProgressChange64: function DLT_onProgressChange64(
    aWebProgress,
    aRequest,
    aCurSelfProgress,
    aMaxSelfProgress,
    aCurTotalProgress,
    aMaxTotalProgress
  ) {
    // Since this progress function is invoked frequently, we use a slightly
    // more complex solution that optimizes the case where we already have an
    // associated Download object, avoiding the Promise overhead.
    if (this._download) {
      this._hasDelayedProgress = false;
      this._download.saver.onProgressBytes(
        aCurTotalProgress,
        aMaxTotalProgress
      );
      return;
    }

    // If we don't have a Download object yet, store the most recent progress
    // notification to send later. We must do this because there is no guarantee
    // that a future notification will be sent if the download stalls.
    this._delayedCurTotalProgress = aCurTotalProgress;
    this._delayedMaxTotalProgress = aMaxTotalProgress;

    // Do not enqueue multiple callbacks for the progress report.
    if (this._hasDelayedProgress) {
      return;
    }
    this._hasDelayedProgress = true;

    this._promiseDownload
      .then(download => {
        // Check whether an immediate progress report has been already processed
        // before we could send the delayed progress report.
        if (!this._hasDelayedProgress) {
          return;
        }
        download.saver.onProgressBytes(
          this._delayedCurTotalProgress,
          this._delayedMaxTotalProgress
        );
      })
      .catch(console.error);
  },
  _hasDelayedProgress: false,
  _delayedCurTotalProgress: 0,
  _delayedMaxTotalProgress: 0,

  // nsIWebProgressListener2
  onRefreshAttempted: function DLT_onRefreshAttempted() {
    // Indicate that refreshes and redirects are allowed by default.  However,
    // note that download components don't usually call this method at all.
    return true;
  },

  // nsITransfer
  init: function DLT_init(
    aSource,
    aSourceOriginalURI,
    aTarget,
    aDisplayName,
    aMIMEInfo,
    aStartTime,
    aTempFile,
    aCancelable,
    aIsPrivate,
    aDownloadClassification,
    aReferrerInfo,
    aOpenDownloadsListOnStart
  ) {
    return this._nsITransferInitInternal(
      aSource,
      aSourceOriginalURI,
      aTarget,
      aDisplayName,
      aMIMEInfo,
      aStartTime,
      aTempFile,
      aCancelable,
      aIsPrivate,
      aDownloadClassification,
      aReferrerInfo,
      aOpenDownloadsListOnStart
    );
  },

  // nsITransfer
  initWithBrowsingContext(
    aSource,
    aTarget,
    aDisplayName,
    aMIMEInfo,
    aStartTime,
    aTempFile,
    aCancelable,
    aIsPrivate,
    aDownloadClassification,
    aReferrerInfo,
    aOpenDownloadsListOnStart,
    aBrowsingContext,
    aHandleInternally,
    aHttpChannel
  ) {
    let browsingContextId;
    let userContextId;
    if (aBrowsingContext && aBrowsingContext.currentWindowGlobal) {
      browsingContextId = aBrowsingContext.id;
      let windowGlobal = aBrowsingContext.currentWindowGlobal;
      let originAttributes = windowGlobal.documentPrincipal.originAttributes;
      userContextId = originAttributes.userContextId;
    }
    return this._nsITransferInitInternal(
      aSource,
      null,
      aTarget,
      aDisplayName,
      aMIMEInfo,
      aStartTime,
      aTempFile,
      aCancelable,
      aIsPrivate,
      aDownloadClassification,
      aReferrerInfo,
      aOpenDownloadsListOnStart,
      userContextId,
      browsingContextId,
      aHandleInternally,
      aHttpChannel
    );
  },

  _nsITransferInitInternal(
    aSource,
    aSourceOriginalURI,
    aTarget,
    aDisplayName,
    aMIMEInfo,
    aStartTime,
    aTempFile,
    aCancelable,
    isPrivate,
    aDownloadClassification,
    referrerInfo,
    openDownloadsListOnStart = true,
    userContextId = 0,
    browsingContextId = 0,
    handleInternally = false,
    aHttpChannel = null
  ) {
    this._cancelable = aCancelable;
    this._determineFilenameCalledBeforeDialog =
      lazy.DownloadIntegration.wasLauncherProcessed(aCancelable);
    let launchWhenSucceeded = false,
      contentType = null,
      launcherPath = null,
      launcherId = null;

    if (aMIMEInfo instanceof Ci.nsIMIMEInfo) {
      launchWhenSucceeded =
        aMIMEInfo.preferredAction != Ci.nsIMIMEInfo.saveToDisk;
      contentType = aMIMEInfo.type;

      let appHandler = aMIMEInfo.preferredApplicationHandler;
      if (aMIMEInfo.preferredAction == Ci.nsIMIMEInfo.useHelperApp) {
        if (appHandler instanceof Ci.nsILocalHandlerApp) {
          launcherPath = appHandler.executable.path;
        } else if (appHandler instanceof Ci.nsIGIOHandlerApp) {
          launcherId = appHandler.id;
        }
      }
    }
    // Create a new Download object associated to a DownloadLegacySaver, and
    // wait for it to be available.  This operation may cause the entire
    // download system to initialize before the object is created.
    let authHeader = null;
    if (aHttpChannel) {
      try {
        authHeader = aHttpChannel.getRequestHeader("Authorization");
      } catch (e) {}
    }
    let serialisedDownload = {
      source: {
        url: aSource.spec,
        originalUrl: aSourceOriginalURI && aSourceOriginalURI.spec,
        isPrivate,
        userContextId,
        browsingContextId,
        referrerInfo,
        authHeader,
      },
      target: {
        path: aTarget.QueryInterface(Ci.nsIFileURL).file.path,
        partFilePath: aTempFile && aTempFile.path,
      },
      saver: "legacy",
      launchWhenSucceeded,
      contentType,
      launcherPath,
      launcherId,
      handleInternally,
      openDownloadsListOnStart,
    };

    // In case the Download was classified as insecure/dangerous
    // it is already canceled, so we need to generate and attach the
    // corresponding error to the download.
    if (aDownloadClassification == Ci.nsITransfer.DOWNLOAD_POTENTIALLY_UNSAFE) {
      Glean.downloads.userActionOnBlockedDownload[
        lazy.DownloadError.BLOCK_VERDICT_INSECURE
      ].accumulateSingleSample(0);

      serialisedDownload.errorObj = {
        becauseBlockedByReputationCheck: true,
        reputationCheckVerdict: lazy.DownloadError.BLOCK_VERDICT_INSECURE,
      };
      // hasBlockedData needs to be true
      // because the unblock UI is hidden if there is
      // no data to be unblocked.
      serialisedDownload.hasBlockedData = true;
      // We cannot use the legacy saver here, as the original channel
      // is already closed. A copy saver would create a new channel once
      // start() is called.
      serialisedDownload.saver = "copy";

      // Since the download is canceled already, we do not need to keep refrences
      this._download = null;
      this._cancelable = null;
    }

    lazy.Downloads.createDownload(serialisedDownload)
      .then(async aDownload => {
        // Legacy components keep partial data when they use a ".part" file.
        if (aTempFile) {
          aDownload.tryToKeepPartialData = true;
        }

        // If onDeterminingFilename was already fired by HelperAppDlg before
        // the Save As dialog (save-to-disk path), prevent Download.start()
        // from firing it again.  Downloads that bypassed HelperAppDlg (e.g.
        // SetDownloadToLaunch for helper-app opens) go through normally.
        if (this._determineFilenameCalledBeforeDialog) {
          lazy.DownloadIntegration.markFilenameAlreadyDetermined(aDownload);
        }

        // Start the download before allowing it to be controlled.  Ignore errors.
        aDownload.start().catch(() => {});

        // Start processing all the other events received through nsITransfer.
        this._download = aDownload;
        this._resolveDownload(aDownload);

        // Add the download to the list, allowing it to be seen and canceled.
        await (await lazy.Downloads.getList(lazy.Downloads.ALL)).add(aDownload);
        if (serialisedDownload.errorObj) {
          // In case we added an already canceled dummy download
          // we need to manually trigger a change event
          // as all the animations for finishing downloads are
          // listening on onChange.
          aDownload._notifyChange();
        }
      })
      .catch(console.error);
  },

  get downloadPromise() {
    return this._promiseDownload;
  },

  setSha256Hash(hash) {
    this._sha256Hash = hash;
  },

  setSignatureInfo(signatureInfo) {
    this._signatureInfo = signatureInfo;
  },

  setRedirects(redirects) {
    this._redirects = redirects;
  },

  /**
   * Download object associated with this nsITransfer instance. This is not
   * available immediately when the nsITransfer instance is created.
   */
  _download: null,

  /**
   * Promise that resolves to the Download object associated with this
   * nsITransfer instance after the _resolveDownload method is invoked.
   *
   * Waiting on this promise using "then" ensures that the callbacks are invoked
   * in the correct order even if enqueued before the object is available.
   */
  _promiseDownload: null,
  _resolveDownload: null,

  /**
   * Reference to the component that is executing the download.  This component
   * allows cancellation through its nsICancelable interface.
   */
  _cancelable: null,

  /**
   * Indicates that the component that executes the download has notified a
   * failure condition.  In this case, we should never use the component methods
   * that cancel the download.
   */
  _componentFailed: false,

  /**
   * Save the SHA-256 hash in raw bytes of the downloaded file.
   */
  _sha256Hash: null,

  /**
   * Save the signature info in a serialized protobuf of the downloaded file.
   */
  _signatureInfo: null,
};
