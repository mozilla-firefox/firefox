/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Provides functions to integrate with the host application, handling for
 * example the global prompts on shutdown.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

import { Downloads } from "resource://gre/modules/Downloads.sys.mjs";
import { Integration } from "resource://gre/modules/Integration.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  ContentAnalysisUtils: "resource://gre/modules/ContentAnalysisUtils.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  DownloadSpamProtection:
    "moz-src:///browser/components/downloads/DownloadSpamProtection.sys.mjs",
  DownloadStore: "resource://gre/modules/DownloadStore.sys.mjs",
  DownloadUIHelper: "resource://gre/modules/DownloadUIHelper.sys.mjs",
  DownloadsCommon:
    "moz-src:///browser/components/downloads/DownloadsCommon.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gDownloadPlatform",
  "@mozilla.org/toolkit/download-platform;1",
  Ci.mozIDownloadPlatform
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gMIMEService",
  "@mozilla.org/mime;1",
  Ci.nsIMIMEService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gExternalProtocolService",
  "@mozilla.org/uriloader/external-protocol-service;1",
  Ci.nsIExternalProtocolService
);

ChromeUtils.defineLazyGetter(lazy, "gParentalControlsService", function () {
  if ("@mozilla.org/parental-controls-service;1" in Cc) {
    return Cc["@mozilla.org/parental-controls-service;1"].getService(
      Ci.nsIParentalControlsService
    );
  }
  return null;
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gApplicationReputationService",
  "@mozilla.org/reputationservice/application-reputation-service;1",
  Ci.nsIApplicationReputationService
);

Integration.downloads.defineESModuleGetter(
  lazy,
  "DownloadIntegration",
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);
ChromeUtils.defineLazyGetter(lazy, "gCombinedDownloadIntegration", () => {
  return lazy.DownloadIntegration;
});

ChromeUtils.defineLazyGetter(lazy, "stringBundle", () =>
  Services.strings.createBundle(
    "chrome://mozapps/locale/downloads/downloads.properties"
  )
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gExternalAppLauncher",
  "@mozilla.org/uriloader/external-helper-app-service;1",
  Ci.nsPIExternalAppLauncher
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "powerManager",
  "@mozilla.org/power/powermanagerservice;1",
  Ci.nsIPowerManagerService
);

const Timer = Components.Constructor(
  "@mozilla.org/timer;1",
  "nsITimer",
  "initWithCallback"
);

/**
 * Indicates the delay between a change to the downloads data and the related
 * save operation.
 *
 * For best efficiency, this value should be high enough that the input/output
 * for opening or closing the target file does not overlap with the one for
 * saving the list of downloads.
 */
const kSaveDelayMs = 1500;

/**
 * List of observers to listen against
 */
const kObserverTopics = [
  "quit-application-requested",
  "quit-application-granted",
  "offline-requested",
  "last-pb-context-exiting",
  "last-pb-context-exited",
  "sleep_notification",
  "suspend_process_notification",
  "wake_notification",
  "resume_process_notification",
  "network:offline-about-to-go-offline",
  "network:offline-status-changed",
  "xpcom-will-shutdown",
  "blocked-automatic-download",
];

/**
 * Provides functions to integrate with the host application, handling for
 * example the global prompts on shutdown.
 */
export var DownloadIntegration = {
  /**
   * Main DownloadStore object for loading and saving the list of persistent
   * downloads, or null if the download list was never requested and thus it
   * doesn't need to be persisted.
   */
  _store: null,

  /**
   * Returns whether data for blocked downloads should be kept on disk.
   * Implementations which support unblocking downloads may return true to
   * keep the blocked download on disk until its fate is decided.
   *
   * If a download is blocked and the partial data is kept the Download's
   * 'hasBlockedData' property will be true. In this state Download.unblock()
   * or Download.confirmBlock() may be used to either unblock the download or
   * remove the downloaded data respectively.
   *
   * Even if shouldKeepBlockedData returns true, if the download did not use a
   * partFile the blocked data will be removed - preventing the complete
   * download from existing on disk with its final filename.
   *
   * @return boolean True if data should be kept.
   */
  shouldKeepBlockedData() {
    const FIREFOX_ID = "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}";
    return Services.appinfo.ID == FIREFOX_ID;
  },

  /**
   * Performs initialization of the list of persistent downloads, before its
   * first use by the host application.  This function may be called only once
   * during the entire lifetime of the application.
   *
   * @param list
   *        DownloadList object to be initialized.
   *
   * @returns {Promise<void>}
   *   Resolves when the list has been initialized.
   * @rejects JavaScript exception.
   */
  async initializePublicDownloadList(list) {
    try {
      await this.loadPublicDownloadListFromStore(list);
    } catch (ex) {
      console.error(ex);
    }

    if (AppConstants.MOZ_PLACES) {
      // After the list of persistent downloads has been loaded, we can add the
      // history observers, even if the load operation failed. This object is kept
      // alive by the history service.
      new DownloadHistoryObserver(list);
    }
  },

  /**
   * Called by initializePublicDownloadList to load the list of persistent
   * downloads, before its first use by the host application.  This function may
   * be called only once during the entire lifetime of the application.
   *
   * @param list
   *        DownloadList object to be populated with the download objects
   *        serialized from the previous session.  This list will be persisted
   *        to disk during the session lifetime.
   *
   * @returns {Promise<void>}
   *   Resolves when the list has been populated.
   * @rejects JavaScript exception.
   */
  async loadPublicDownloadListFromStore(list) {
    if (this._store) {
      throw new Error("Initialization may be performed only once.");
    }

    this._store = new lazy.DownloadStore(
      list,
      PathUtils.join(PathUtils.profileDir, "downloads.json")
    );
    this._store.onsaveitem = this.shouldPersistDownload.bind(this);

    try {
      await this._store.load();
    } catch (ex) {
      console.error(ex);
    }

    // Add the view used for detecting changes to downloads to be persisted.
    // We must do this after the list of persistent downloads has been loaded,
    // even if the load operation failed. We wait for a complete initialization
    // so other callers cannot modify the list without being detected. The
    // DownloadAutoSaveView is kept alive by the underlying DownloadList.
    new DownloadAutoSaveView(list, this._store).initialize();
  },

  /**
   * Determines if a Download object from the list of persistent downloads
   * should be saved into a file, so that it can be restored across sessions.
   *
   * This function allows filtering out downloads that the host application is
   * not interested in persisting across sessions, for example downloads that
   * finished successfully.
   *
   * @param aDownload
   *        The Download object to be inspected.  This is originally taken from
   *        the global DownloadList object for downloads that were not started
   *        from a private browsing window.  The item may have been removed
   *        from the list since the save operation started, though in this case
   *        the save operation will be repeated later.
   *
   * @return True to save the download, false otherwise.
   */
  shouldPersistDownload(aDownload) {
    // On all platforms, we save all the downloads currently in progress, as
    // well as stopped downloads for which we retained partially downloaded
    // data or we have blocked data.
    // On Android we store all history; on Desktop, stopped downloads for which
    // we don't need to track the presence of a ".part" file are only retained
    // in the browser history.
    return (
      !aDownload.stopped ||
      aDownload.hasPartialData ||
      aDownload.hasBlockedData ||
      AppConstants.platform == "android"
    );
  },

  /**
   * Returns the system downloads directory asynchronously.
   *
   * @returns {Promise<string>}
   *   Resolves to the downloads directory path.
   */
  async getSystemDownloadsDirectory() {
    if (this._downloadsDirectory) {
      return this._downloadsDirectory;
    }

    if (AppConstants.platform == "android") {
      // Android doesn't have a $HOME directory, and by default we only have
      // write access to /data/data/org.mozilla.{$APP} and /sdcard
      this._downloadsDirectory = Services.env.get("DOWNLOADS_DIRECTORY");
      if (!this._downloadsDirectory) {
        throw new Components.Exception(
          "DOWNLOADS_DIRECTORY is not set.",
          Cr.NS_ERROR_FILE_UNRECOGNIZED_PATH
        );
      }
    } else {
      try {
        this._downloadsDirectory = this._getDirectory("DfltDwnld");
      } catch (e) {
        this._downloadsDirectory = await this._createDownloadsDirectory("Home");
      }
    }

    return this._downloadsDirectory;
  },
  _downloadsDirectory: null,

  /**
   * Get the path of the directory from the provided preference.
   * If the preference is not set or the directory does not exist,
   * return the path of the downloads directory.
   *
   * @param {string} pref
   *        The preference which contains the directory
   *
   * @returns {Promise<string>}
   *   Resolves to the directory path.
   */
  async _getCustomDirectoryOrDownloads(pref) {
    let directoryPath = null;
    try {
      let directory = Services.prefs.getComplexValue(pref, Ci.nsIFile);
      directoryPath = directory.path;
      await IOUtils.makeDirectory(directoryPath, {
        createAncestors: false,
      });
    } catch (ex) {
      console.error(ex);
      // Either the preference isn't set or the directory cannot be created.
      directoryPath = await this.getSystemDownloadsDirectory();
    }
    return directoryPath;
  },

  /**
   * Returns the user downloads directory asynchronously.
   *
   * On platforms where external helper apps use the downloads directory, the
   * behavior should match that of the synchronous function of the same name
   * exposed by nsIExternalHelperAppService.
   *
   * @returns {Promise<string>}
   *   Resolves to the downloads directory path.
   */
  async getPreferredDownloadsDirectory() {
    let directoryPath = null;
    let prefValue = Services.prefs.getIntPref("browser.download.folderList", 1);

    switch (prefValue) {
      case 0: // Desktop
        directoryPath = this._getDirectory("Desk");
        break;
      case 1: // Downloads
        directoryPath = await this.getSystemDownloadsDirectory();
        break;
      case 2: // Custom
        directoryPath = await this._getCustomDirectoryOrDownloads(
          "browser.download.dir"
        );
        break;
      default:
        directoryPath = await this.getSystemDownloadsDirectory();
    }
    return directoryPath;
  },

  /**
   * Returns the user screenshots directory asynchronously.
   *
   * @returns {Promise<string>}
   *   Resolves to the screenshots directory path.
   */
  async getPreferredScreenshotsDirectory() {
    let directoryPath = null;
    let prefValue = Services.prefs.getIntPref(
      "browser.screenshots.folderList",
      4
    );

    switch (prefValue) {
      case 0: // Desktop
        directoryPath = this._getDirectory("Desk");
        break;
      case 1: // Downloads
        directoryPath = await this.getSystemDownloadsDirectory();
        break;
      case 2: // Custom
        directoryPath = await this._getCustomDirectoryOrDownloads(
          "browser.screenshots.dir"
        );
        break;
      case 3: // Default OS screenshots directory
        try {
          directoryPath = this._getDirectory("Scrnshts");
        } catch {
          directoryPath = await this.getSystemDownloadsDirectory();
        }
        break;
      case 4: // Fallback to preferred downloads
        directoryPath = this.getPreferredDownloadsDirectory();
        break;
      default:
        directoryPath = await this.getSystemDownloadsDirectory();
    }
    return directoryPath;
  },

  /**
   * Returns the temporary downloads directory asynchronously.
   *
   * @returns {Promise<string>}
   *   Resolves to the temporary downloads directory path.
   */
  async getTemporaryDownloadsDirectory() {
    let directoryPath = null;
    if (AppConstants.platform == "macosx") {
      directoryPath = await this.getPreferredDownloadsDirectory();
    } else if (AppConstants.platform == "android") {
      directoryPath = await this.getSystemDownloadsDirectory();
    } else {
      directoryPath = this._getDirectory("TmpD");
    }
    return directoryPath;
  },

  /**
   * Checks to determine whether to block downloads for parental controls.
   *
   * aParam aDownload
   *        The download object.
   *
   * @returns {Promise<boolean>}
   *   Resolves to a boolean which indicates to block downloads or not.
   */
  shouldBlockForParentalControls(aDownload) {
    let isEnabled =
      lazy.gParentalControlsService &&
      lazy.gParentalControlsService.parentalControlsEnabled;
    let shouldBlock =
      isEnabled && lazy.gParentalControlsService.blockFileDownloadsEnabled;

    // Log the event if required by parental controls settings.
    if (isEnabled && lazy.gParentalControlsService.loggingEnabled) {
      lazy.gParentalControlsService.log(
        lazy.gParentalControlsService.ePCLog_FileDownload,
        shouldBlock,
        lazy.NetUtil.newURI(aDownload.source.url),
        null
      );
    }

    return Promise.resolve(shouldBlock);
  },

  /**
   * Checks to determine whether to block downloads because they might be
   * malware, based on application reputation checks.
   *
   * aParam aDownload
   *        The download object.
   *
   * @returns {Promise}
   *   Resolves to an object with the following properties:
   *   {
   *     shouldBlock: Whether the download should be blocked.
   *     verdict: Detailed reason for the block, according to the
   *              "Downloads.Error.BLOCK_VERDICT_" constants, or empty
   *              string if the reason is unknown.
   *   }
   */
  shouldBlockForReputationCheck(aDownload) {
    let hash;
    let sigInfo;
    let channelRedirects;
    try {
      hash = aDownload.saver.getSha256Hash();
      sigInfo = aDownload.saver.getSignatureInfo();
      channelRedirects = aDownload.saver.getRedirects();
    } catch (ex) {
      // Bail if DownloadSaver doesn't have a hash or signature info.
      return Promise.resolve({
        shouldBlock: false,
        verdict: Ci.nsIApplicationReputationService.VERDICT_SAFE,
      });
    }
    if (!hash || !sigInfo) {
      return Promise.resolve({
        shouldBlock: false,
        verdict: Ci.nsIApplicationReputationService.VERDICT_SAFE,
      });
    }
    return new Promise(resolve => {
      lazy.gApplicationReputationService.queryReputation(
        {
          sourceURI: lazy.NetUtil.newURI(aDownload.source.url),
          referrerInfo: aDownload.source.referrerInfo,
          fileSize: aDownload.currentBytes,
          sha256Hash: hash,
          suggestedFileName: PathUtils.filename(aDownload.target.path),
          signatureInfo: sigInfo,
          redirects: channelRedirects,
        },
        function onComplete(aShouldBlock, aRv, aVerdict) {
          resolve({
            shouldBlock: aShouldBlock,
            verdict: aShouldBlock
              ? aVerdict
              : Ci.nsIApplicationReputationService.VERDICT_SAFE,
          });
        }
      );
    });
  },

  getContentAnalysisService() {
    // Do not use a lazy service getter for this, because tests set up different mocks,
    // so if multiple tests run that call into this we can end up calling into an old mock.
    return Cc["@mozilla.org/contentanalysis;1"].getService(
      Ci.nsIContentAnalysis
    );
  },

  async shouldBlockForContentAnalysis(download) {
    const contentAnalysis = this.getContentAnalysisService();

    if (!contentAnalysis.isActive) {
      return {
        verdict: Ci.nsIApplicationReputationService.VERDICT_SAFE,
        shouldBlock: false,
      };
    }

    // For PDF files loaded in pdf.js the originalUrl is the original URL
    // where the PDF was loaded from, and the url is the URL of the pdf.js
    // resource.
    let downloadUrl = download.source.originalUrl ?? download.source.url;
    let resources = [
      {
        url: downloadUrl,
        type: Ci.nsIClientDownloadResource.DOWNLOAD_URL,
      },
    ];

    let redirects = download.saver.getRedirects();
    if (redirects) {
      for (let redirect of redirects.enumerate()) {
        resources.push({
          url: redirect.referrerURI,
          type: Ci.nsIClientDownloadResource.DOWNLOAD_REDIRECT,
        });
      }
    }

    // source.referrerInfo is a string or nsIReferrerInfo that
    // represents the download referrer.  May be null.
    if (download.source.referrerInfo) {
      const url =
        download.source.referrerInfo instanceof Ci.nsIReferrerInfo
          ? download.source.referrerInfo.originalReferrer?.spec
          : download.source.referrerInfo;
      if (url) {
        resources.push({
          url,
          type: Ci.nsIClientDownloadResource.TAB_URL,
        });
      }
    }

    let url = lazy.NetUtil.newURI(downloadUrl);
    let fileNameForDisplay = download.target.path;
    try {
      // Try to get a prettier name
      let file = new lazy.FileUtils.File(download.target.path);
      fileNameForDisplay = file.displayName;
    } catch (ex) {
      // oh well
    }
    const requestToken = Services.uuid.generateUUID().toString();
    const userActionId = Services.uuid.generateUUID().toString();
    let warnResponseObserver = undefined;
    // Set up a separate promise to wait specifically for a WARN
    // response (if it comes) while we also wait for a final response.
    // This is necessary because if the agent sends a WARN response,
    // it doesn't count as a real response, and the Content Analysis code
    // won't respond to the callback until respondToWarnDialog() is called.
    const warnResultPromise = new Promise(resolve => {
      warnResponseObserver = function (subject, topic, _data) {
        if (topic == "dlp-response") {
          /** @type nsIContentAnalysisResponse */
          let response = subject;
          if (
            response.requestToken === requestToken &&
            response.action === Ci.nsIContentAnalysisResponse.eWarn
          ) {
            resolve({
              isContentAnalysisWarn: true,
              verdict:
                Ci.nsIApplicationReputationService.VERDICT_POTENTIALLY_UNWANTED,
              contentAnalysisWarnRequestToken: requestToken,
              shouldBlock: true,
            });
          }
        }
      };
      Services.obs.addObserver(warnResponseObserver, "dlp-response");
    });
    let cancelPromiseWithResolvers = Promise.withResolvers();
    DownloadObserver._contentAnalysisInProgressDownloads.set(
      download,
      cancelPromiseWithResolvers
    );
    let finalResultPromise = contentAnalysis
      .analyzeContentRequests(
        [
          lazy.ContentAnalysisUtils.createContentAnalysisRequest(
            {
              analysisType: Ci.nsIContentAnalysisRequest.eFileDownloaded,
              operationTypeForDisplay: Ci.nsIContentAnalysisRequest.eDownload,
              // "Save As" downloads do not have a browsing context
              reason:
                download.source.browsingContextId === 0
                  ? Ci.nsIContentAnalysisRequest.eSaveAsDownload
                  : Ci.nsIContentAnalysisRequest.eNormalDownload,
              url,
              // When doing a download analysis, the Content Analysis code won't
              // display dialogs in the window, but the code still wants a
              // content window and will get the topChromeWindow to show
              // a notification.
              windowGlobalParent: BrowsingContext.get(
                download.source.browsingContextId
              )?.topWindowContext,
            },
            {
              fileNameForDisplay,
              resources,
              requestToken,
              userActionId,
              // Use the path to the part-file if a part file is involved.
              // We haven't moved it yet.
              filePath: download.target.partFilePath || download.target.path,
              sha256Digest: download.saver.getSha256Hash(),
            }
          ),
        ],
        /* autoAcknowledge*/ true
      )
      .then(response => {
        let cancelError;
        if (response?.action === Ci.nsIContentAnalysisResponse.eCanceled) {
          cancelError = response.cancelError;
        }
        return {
          verdict: response.shouldAllowContent
            ? Ci.nsIApplicationReputationService.VERDICT_SAFE
            : Ci.nsIApplicationReputationService.VERDICT_DANGEROUS,
          shouldBlock: !response.shouldAllowContent,
          contentAnalysisCancelError: cancelError,
          contentAnalysisWarnRequestToken: undefined,
        };
      });
    try {
      let finalOrWarnResult = await Promise.race([
        finalResultPromise,
        warnResultPromise,
        cancelPromiseWithResolvers.promise,
      ]);
      if (finalOrWarnResult.canceled) {
        contentAnalysis.cancelRequestsByUserAction(userActionId);
        // The cancel will override this, so just return something
        return {
          verdict: Ci.nsIApplicationReputationService.VERDICT_SAFE,
          shouldBlock: false,
        };
      }
      return finalOrWarnResult;
    } catch (e) {
      console.error(e);
      return {
        verdict: Ci.nsIApplicationReputationService.VERDICT_DANGEROUS,
        shouldBlock: true,
      };
    } finally {
      Services.obs.removeObserver(warnResponseObserver, "dlp-response");
      DownloadObserver._contentAnalysisInProgressDownloads.delete(download);
    }
  },

  /**
   * Performs platform-specific operations when a download is done.
   *
   * aParam aDownload
   *        The Download object.
   *
   * @returns {Promise<void>}
   *   Resolves when all the operations completed successfully.
   * @rejects JavaScript exception if any of the operations failed.
   */
  async downloadDone(aDownload) {
    // For PDF files loaded in pdf.js the originalUrl is the original URL
    // where the PDF was loaded from, and the url is a blob URL: the former is
    // the one worth recording in the file metadata.
    const sourceUrl = aDownload.source.originalUrl ?? aDownload.source.url;

    try {
      // On Windows, this will mark any file saved to the file system as coming
      // from the Internet security zone unless Group Policy disables the
      // feature.  We do this by writing to the "Zone.Identifier" Alternate
      // Data Stream directly, because the Save method of the
      // IAttachmentExecute interface would trigger operations that may cause
      // the application to hang, or other performance issues.
      // The stream created in this way is forward-compatible with all the
      // current and future versions of Windows.
      // This currently does nothing on other platforms.
      await lazy.gDownloadPlatform.maybeWriteDownloadOriginInformation(
        new lazy.FileUtils.File(aDownload.target.path),
        lazy.NetUtil.newURI(sourceUrl),
        aDownload.source.referrerInfo,
        aDownload.source.isPrivate
      );
    } catch (ex) {
      // Just swallow the error.
      console.error(ex);
    }

    // The file with the partially downloaded data has restrictive permissions
    // that don't allow other users on the system to access it.  Now that the
    // download is completed, we need to adjust permissions based on whether
    // this is a permanently downloaded file or a temporary download to be
    // opened read-only with an external application.
    try {
      let isTemporaryDownload =
        aDownload.launchWhenSucceeded && aDownload.source.isPrivate;
      // Permanently downloaded files are made accessible by other users on
      // this system, while temporary downloads are marked as read-only.
      let unixMode;
      if (isTemporaryDownload) {
        unixMode = 0o400;
      } else {
        unixMode = 0o666;
      }
      // On Unix, the umask of the process is respected.
      await IOUtils.setPermissions(aDownload.target.path, unixMode);
    } catch (ex) {
      // We should report errors with making the permissions less restrictive
      // or marking the file as read-only on Unix and Mac, but this should not
      // prevent the download from completing.
      if (!DOMException.isInstance(ex)) {
        console.error(ex);
      }
    }

    // If the download has a referrer, we pass the computed referrer spec to
    // the platform to update any file meta-data. The computedReferrerSpec is
    // the referrer URL after all referrer sanitization and policy checks have
    // been applied.
    let aReferrer = null;
    if (aDownload.source.referrerInfo?.computedReferrerSpec) {
      aReferrer = lazy.NetUtil.newURI(
        aDownload.source.referrerInfo.computedReferrerSpec
      );
    }

    await lazy.gDownloadPlatform.downloadDone(
      lazy.NetUtil.newURI(sourceUrl),
      aReferrer,
      new lazy.FileUtils.File(aDownload.target.path),
      aDownload.contentType,
      aDownload.source.isPrivate
    );
  },

  /**
   * Decide whether a download of this type, opened from the downloads
   * list, should open internally.
   *
   * @param aMimeType
   *        The MIME type of the file, as a string
   * @param [optional] aExtension
   *        The file extension, which can match instead of the MIME type.
   */
  shouldViewDownloadInternally() {
    // Refuse all files by default, this is meant to be replaced with a check
    // for specific types via Integration.downloads.register().
    return false;
  },

  /**
   * Launches a file represented by the target of a download. This can
   * open the file with the default application for the target MIME type
   * or file extension, or with a custom application if
   * aDownload.launcherPath or aDownload.launcherId is set.
   *
   * @param    aDownload
   *           A Download object that contains the necessary information
   *           to launch the file. The relevant properties are: the target
   *           file, the contentType and the custom application chosen
   *           to launch it.
   * @param options.openWhere     Optional string indicating how to open when handling
   *                              download by opening the target file URI.
   *                              One of "window", "tab", "tabshifted"
   * @param options.useSystemDefault
   *                              Optional value indicating how to handle launching this download,
   *                              this time only. Will override the associated mimeInfo.preferredAction
   *
   * @returns {Promise<void>}
   *   Resolves when the instruction to launch the file has been successfully
   *   given to the operating system. Note that the OS might still take a while
   *   until the file is actually launched.
   * @rejects JavaScript exception if there was an error trying to launch
   *          the file.
   */
  async launchDownload(aDownload, { openWhere, useSystemDefault = null }) {
    let file = new lazy.FileUtils.File(aDownload.target.path);

    // In case of a double extension, like ".tar.gz", we only
    // consider the last one, because the MIME service cannot
    // handle multiple extensions.
    let fileExtension = null,
      mimeInfo = null;
    let match = file.leafName.match(/\.([^.]+)$/);
    if (match) {
      fileExtension = match[1];
    }

    let isWindowsExe =
      AppConstants.platform == "win" &&
      fileExtension &&
      fileExtension.toLowerCase() == "exe";

    let isExemptExecutableExtension =
      Services.policies.isExemptExecutableExtension(
        aDownload.source.url,
        fileExtension
      );

    // Ask for confirmation if the file is executable, except for .exe on
    // Windows where the operating system will show the prompt based on the
    // security zone.  We do this here, instead of letting the caller handle
    // the prompt separately in the user interface layer, for two reasons.  The
    // first is because of its security nature, so that add-ons cannot forget
    // to do this check.  The second is that the system-level security prompt
    // would be displayed at launch time in any case.
    // We allow policy to override this behavior for file extensions on specific domains.
    if (
      file.isExecutable() &&
      !isWindowsExe &&
      !isExemptExecutableExtension &&
      !(await this.confirmLaunchExecutable(file.path))
    ) {
      return;
    }

    try {
      // The MIME service might throw if contentType == "" and it can't find
      // a MIME type for the given extension, so we'll treat this case as
      // an unknown mimetype.
      mimeInfo = lazy.gMIMEService.getFromTypeAndExtension(
        aDownload.contentType,
        fileExtension
      );
    } catch (e) {}

    // An attempt will be made to launch the download, thus clear the
    // `launchWhenSucceeded` flag, so future opens can go through Firefox, and
    // not be treated as auto-opens. This is done early to ensure it's cleared
    // in case of early returns below, but we still need the current value
    // for this call.
    let launchWhenSucceeded = aDownload.launchWhenSucceeded;
    aDownload.launchWhenSucceeded = false;

    if (aDownload.launcherPath || aDownload.launcherId) {
      if (!mimeInfo) {
        // This should not happen on normal circumstances because launcherPath
        // is only set when we had an instance of nsIMIMEInfo to retrieve
        // the custom application chosen by the user.
        throw new Error(
          "Unable to create nsIMIMEInfo to launch a custom application"
        );
      }
      let localHandlerApp = null;

      if (aDownload.launcherId) {
        if (!Cc["@mozilla.org/gio-service;1"]) {
          throw new Error(
            "The launcherId is set but missing gio-service to create nsGIOHandlerApp"
          );
        }
        localHandlerApp = Cc["@mozilla.org/gio-service;1"]
          .getService(Ci.nsIGIOService)
          .createHandlerAppFromAppId(aDownload.launcherId);
      } else {
        // Custom application chosen
        localHandlerApp = Cc[
          "@mozilla.org/uriloader/local-handler-app;1"
        ].createInstance(Ci.nsILocalHandlerApp);
        localHandlerApp.executable = new lazy.FileUtils.File(
          aDownload.launcherPath
        );
      }

      mimeInfo.preferredApplicationHandler = localHandlerApp;
      mimeInfo.preferredAction = Ci.nsIMIMEInfo.useHelperApp;

      this.launchFile(file, mimeInfo);
      return;
    }

    if (!useSystemDefault && mimeInfo) {
      useSystemDefault = mimeInfo.preferredAction == mimeInfo.useSystemDefault;
    }
    if (!useSystemDefault) {
      // No explicit instruction was passed to launch this download using the default system viewer.
      if (
        aDownload.handleInternally ||
        (mimeInfo &&
          this.shouldViewDownloadInternally(mimeInfo.type, fileExtension) &&
          !mimeInfo.alwaysAskBeforeHandling &&
          (mimeInfo.preferredAction === Ci.nsIHandlerInfo.handleInternally ||
            (["image/svg+xml", "text/xml", "application/xml"].includes(
              mimeInfo.type
            ) &&
              mimeInfo.preferredAction === Ci.nsIHandlerInfo.saveToDisk)) &&
          !launchWhenSucceeded)
      ) {
        lazy.DownloadUIHelper.loadFileIn(file, {
          browsingContextId: aDownload.source.browsingContextId,
          isPrivate: aDownload.source.isPrivate,
          openWhere,
          userContextId: aDownload.source.userContextId,
          openInBackgroundIfSwitchedBrowsingContext: launchWhenSucceeded,
        });
        return;
      }
    }

    // When a file has no extension, and there's an executable file with the
    // same name in the same folder, Windows shell can get confused.
    // For this reason we show the file in the containing folder instead of
    // trying to open it.
    // We also don't trust mimeinfo, it could be a type we can forward to a
    // system handler, but it could also be an executable type, and we
    // don't have an exhaustive list with all of them.
    if (!fileExtension && AppConstants.platform == "win") {
      // We can't check for the existance of a same-name file with every
      // possible executable extension, so this is a catch-all.
      this.showContainingDirectory(aDownload.target.path);
      return;
    }

    // No custom application chosen, let's launch the file with the default
    // handler. First, let's try to launch it through the MIME service.
    if (mimeInfo) {
      mimeInfo.preferredAction = Ci.nsIMIMEInfo.useSystemDefault;
      try {
        this.launchFile(file, mimeInfo);
        return;
      } catch (ex) {}
    }

    // If it didn't work or if there was no MIME info available,
    // let's try to directly launch the file.
    try {
      this.launchFile(file);
      return;
    } catch (ex) {}

    // If our previous attempts failed, try sending it through
    // the system's external "file:" URL handler.
    lazy.gExternalProtocolService.loadURI(
      lazy.NetUtil.newURI(file),
      Services.scriptSecurityManager.getSystemPrincipal()
    );
  },

  /**
   * Asks for confirmation for launching the specified executable file. This
   * can be overridden by regression tests to avoid the interactive prompt.
   */
  async confirmLaunchExecutable(path) {
    // We don't anchor the prompt to a specific window intentionally, not
    // only because this is the same behavior as the system-level prompt,
    // but also because the most recently active window is the right choice
    // in basically all cases.
    return lazy.DownloadUIHelper.getPrompter().confirmLaunchExecutable(path);
  },

  /**
   * Launches the specified file, unless overridden by regression tests.
   *
   * Note: Always use launchDownload() from the outside of this module, it is
   *       both more powerful and safer.
   */
  launchFile(file, mimeInfo) {
    if (mimeInfo) {
      mimeInfo.launchWithFile(file);
    } else {
      file.launch();
    }
  },

  /**
   * Shows the containing folder of a file.
   *
   * @param aFilePath
   *        The path to the file.
   *
   * @returns {Promise<void>}
   *   Resolves when the instruction to open the containing folder has been
   *   successfully given to the operating system. Note that the OS might
   *   still take a while until the folder is actually opened.
   * @rejects JavaScript exception if there was an error trying to open
   *          the containing folder.
   */
  async showContainingDirectory(aFilePath) {
    let file = new lazy.FileUtils.File(aFilePath);

    try {
      // Show the directory containing the file and select the file.
      file.reveal();
      return;
    } catch (ex) {}

    // If reveal fails for some reason (e.g., it's not implemented on unix
    // or the file doesn't exist), try using the parent if we have it.
    let parent = file.parent;
    if (!parent) {
      throw new Error(
        "Unexpected reference to a top-level directory instead of a file"
      );
    }

    try {
      // Open the parent directory to show where the file should be.
      parent.launch();
      return;
    } catch (ex) {}

    // If launch also fails (probably because it's not implemented), let
    // the OS handler try to open the parent.
    lazy.gExternalProtocolService.loadURI(
      lazy.NetUtil.newURI(parent),
      Services.scriptSecurityManager.getSystemPrincipal()
    );
  },

  /**
   * Calls the directory service, create a downloads directory and returns an
   * nsIFile for the downloads directory.
   *
   * @returns {Promise<string>}
   *   Resolves to the directory path.
   */
  _createDownloadsDirectory(aName) {
    // We read the name of the directory from the list of translated strings
    // that is kept by the UI helper module, even if this string is not strictly
    // displayed in the user interface.
    let directoryPath = PathUtils.join(
      this._getDirectory(aName),
      lazy.stringBundle.GetStringFromName("downloadsFolder")
    );

    // Create the Downloads folder and ignore if it already exists.
    return IOUtils.makeDirectory(directoryPath, {
      createAncestors: false,
    }).then(() => directoryPath);
  },

  /**
   * Returns the string path for the given directory service location name. This
   * can be overridden by regression tests to return the path of the system
   * temporary directory in all cases.
   */
  _getDirectory(name) {
    return Services.dirsvc.get(name, Ci.nsIFile).path;
  },

  /**
   * Initializes the DownloadSpamProtection instance.
   * This is used to observe and group multiple automatic downloads.
   */
  _initializeDownloadSpamProtection() {
    if (!this.downloadSpamProtection) {
      this.downloadSpamProtection = new lazy.DownloadSpamProtection();
    }
  },

  /**
   * Register the downloads interruption observers.
   *
   * @param aList
   *        The public or private downloads list.
   * @param aIsPrivate
   *        True if the list is private, false otherwise.
   *
   * @returns {Promise<void>}
   *   Reolves when the views and observers are added.
   */
  addListObservers(aList, aIsPrivate) {
    DownloadObserver.registerView(aList, aIsPrivate);
    if (!DownloadObserver.observersAdded) {
      DownloadObserver.observersAdded = true;
      for (let topic of kObserverTopics) {
        Services.obs.addObserver(DownloadObserver, topic);
      }
    }

    Services.prefs.addObserver(
      "browser.download.deletePrivate",
      async function () {
        let privateDownloadsList = await Downloads.getList(Downloads.PRIVATE);
        let privateDownloads = await privateDownloadsList.getAll();
        for (let download of privateDownloads) {
          lazy.gExternalAppLauncher.deletePrivateFileWhenPossible(
            new lazy.FileUtils.File(download.target.path)
          );
        }
      }
    );
    return Promise.resolve();
  },

  /**
   * Force a save on _store if it exists. Used to ensure downloads do not
   * persist after being sanitized on Android.
   *
   * @returns {Promise<void>}
   *   Resolves when _store.save() completes.
   */
  forceSave() {
    if (this._store) {
      return this._store.save();
    }
    return Promise.resolve();
  },

  get _testGetDownloadObserver() {
    return DownloadObserver;
  },
};

var DownloadObserver = {
  /**
   * Flag to determine if the observers have been added previously.
   */
  observersAdded: false,

  /**
   * Timer used to delay restarting canceled downloads upon waking and returning
   * online.
   */
  _wakeTimer: null,

  /**
   * WakeLock used to request OS to refrain from sleeping during a download.
   */
  _downloadWakeLock: null,

  /**
   * Set that contains the in progress publics downloads.
   * It's kept updated when a public download is added, removed or changes its
   * properties.
   */
  _publicInProgressDownloads: new Set(),

  /**
   * Set that contains the in progress private downloads.
   * It's kept updated when a private download is added, removed or changes its
   * properties.
   */
  _privateInProgressDownloads: new Set(),

  /**
   * Set of downloads that have finished but have gotten a content analysis
   * WARN response. These downloads need to be canceled when quitting, because
   * the next time we start Firefox the content analysis agent may not have
   * the same context as the one that was running when it analyzed the
   * download.
   */
  _contentAnalysisWarnInProgressDownloads: new Set(),
  _contentAnalysisInProgressDownloads: new Map(),

  /**
   * Set that contains the downloads that have been canceled when going offline
   * or to sleep. These are started again when returning online or waking. This
   * list is not persisted so when exiting and restarting, the downloads will not
   * be started again.
   */
  _canceledOfflineDownloads: new Set(),

  /**
   * Whether any public or private downloads are currently in progress.
   * Used to decide when to hold the download wake lock.
   *
   * Note: _contentAnalysisWarnInProgressDownloads is intentionally not
   * accounted for here. Those downloads have already finished transferring
   * data and are waiting on a user decision for a content analysis
   * warning.
   */
  get _hasInProgressDownloads() {
    return (
      this._publicInProgressDownloads.size > 0 ||
      this._privateInProgressDownloads.size > 0
    );
  },

  _acquireDownloadWakeLock() {
    if (!this._downloadWakeLock) {
      this._downloadWakeLock = lazy.powerManager.newWakeLock(
        "download-in-progress",
        null
      );
    }
  },

  _releaseDownloadWakeLock() {
    if (this._downloadWakeLock) {
      try {
        this._downloadWakeLock.unlock();
      } catch (e) {
        // Ignore error since wakelock is already unlocked
      }
      this._downloadWakeLock = null;
    }
  },

  /**
   * Registers a view that updates the corresponding downloads state set, based
   * on the aIsPrivate argument. The set is updated when a download is added,
   * removed or changes its properties. A wakelock is used to keep OS from
   * sleeping during active downloads.
   *
   * @param aList
   *        The public or private downloads list.
   * @param aIsPrivate
   *        True if the list is private, false otherwise.
   */
  registerView: function DO_registerView(aList, aIsPrivate) {
    let downloadsSet = aIsPrivate
      ? this._privateInProgressDownloads
      : this._publicInProgressDownloads;
    let downloadsView = {
      onDownloadAdded: aDownload => {
        if (!aDownload.stopped) {
          downloadsSet.add(aDownload);
          if (this._hasInProgressDownloads) {
            this._acquireDownloadWakeLock();
          }
        }
      },
      onDownloadChanged: aDownload => {
        if (aDownload.canceled) {
          let deferred =
            this._contentAnalysisInProgressDownloads.get(aDownload);
          if (deferred) {
            deferred.resolve({ canceled: true });
          }
        }
        if (aDownload.stopped) {
          if (aDownload.error?.contentAnalysisWarnRequestToken) {
            this._contentAnalysisWarnInProgressDownloads.add(aDownload);
          } else {
            this._contentAnalysisWarnInProgressDownloads.delete(aDownload);
            this._contentAnalysisInProgressDownloads.delete(aDownload);
          }
          downloadsSet.delete(aDownload);
          if (!this._hasInProgressDownloads) {
            this._releaseDownloadWakeLock();
          }
        } else {
          downloadsSet.add(aDownload);
          if (this._hasInProgressDownloads) {
            this._acquireDownloadWakeLock();
          }
        }
      },
      onDownloadRemoved: aDownload => {
        downloadsSet.delete(aDownload);
        this._contentAnalysisWarnInProgressDownloads.delete(aDownload);
        this._contentAnalysisInProgressDownloads.delete(aDownload);
        // The download must also be removed from the canceled when offline set.
        this._canceledOfflineDownloads.delete(aDownload);
        if (!this._hasInProgressDownloads) {
          this._releaseDownloadWakeLock();
        }
      },
    };

    // Register the view asynchronously.
    aList.addView(downloadsView);
  },

  /**
   * Wrapper that handles the test mode before calling the prompt that display
   * a warning message box that informs that there are active downloads,
   * and asks whether the user wants to cancel them or not.
   *
   * @param aCancel
   *        The observer notification subject.
   * @param aDownloadsCount
   *        The current downloads count.
   * @param aPrompter
   *        The prompter object that shows the confirm dialog.
   * @param aPromptType
   *        The type of prompt notification depending on the observer.
   */
  _confirmCancelDownloads: function DO_confirmCancelDownload(
    aCancel,
    aDownloadsCount,
    aPromptType
  ) {
    // Handle test mode
    if (lazy.gCombinedDownloadIntegration._testPromptDownloads) {
      lazy.gCombinedDownloadIntegration._testPromptDownloads = aDownloadsCount;
      return;
    }

    if (!aDownloadsCount) {
      return;
    }

    const isPromptGranted = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    isPromptGranted.data = true;
    Services.obs.notifyObservers(
      isPromptGranted,
      "before-cancel-download-prompt"
    );

    if (!isPromptGranted.data) {
      aCancel.data = false;
      return;
    }

    // If user has already dismissed the request, then do nothing.
    if (aCancel instanceof Ci.nsISupportsPRBool && aCancel.data) {
      return;
    }

    let prompter = lazy.DownloadUIHelper.getPrompter();
    aCancel.data = prompter.confirmCancelDownloads(
      aDownloadsCount,
      prompter[aPromptType]
    );
  },

  /**
   * Resume all downloads that were paused when going offline, used when waking
   * from sleep or returning from being offline.
   */
  _resumeOfflineDownloads: function DO_resumeOfflineDownloads() {
    this._wakeTimer = null;

    for (let download of this._canceledOfflineDownloads) {
      download.start().catch(() => {});
    }
    this._canceledOfflineDownloads.clear();
  },

  // nsIObserver
  observe: function DO_observe(aSubject, aTopic, aData) {
    let downloadsCount;
    switch (aTopic) {
      case "quit-application-requested": {
        downloadsCount =
          this._publicInProgressDownloads.size +
          this._privateInProgressDownloads.size +
          this._contentAnalysisWarnInProgressDownloads.size;
        this._confirmCancelDownloads(aSubject, downloadsCount, "ON_QUIT");
        break;
      }
      case "quit-application-granted": {
        let blockPromises = [];
        for (let download of this._contentAnalysisWarnInProgressDownloads) {
          blockPromises.push(
            (async () => {
              // Since the user is blocking this download and we're about to
              // quit, delete the download from the list. (this will also
              // delete the file on disk and report to the DLP agent that
              // we have blocked it)
              await lazy.DownloadsCommon.deleteDownload(download);
            })()
          );
        }
        if (blockPromises.length) {
          // Wait for all the downloads to be blocked (and the files deleted)
          // before proceeding with the quit.
          let promiseDone = false;
          Promise.all(blockPromises).finally(() => {
            promiseDone = true;
          });
          Services.tm.spinEventLoopUntil(
            "DownloadIntegration.sys.mjs:DI_observe_quit-application-granted",
            () => {
              return promiseDone;
            }
          );
        }
        this._contentAnalysisWarnInProgressDownloads.clear();
        this._contentAnalysisInProgressDownloads.clear();
        break;
      }
      case "offline-requested":
        downloadsCount =
          this._publicInProgressDownloads.size +
          this._privateInProgressDownloads.size;
        this._confirmCancelDownloads(aSubject, downloadsCount, "ON_OFFLINE");
        break;
      case "last-pb-context-exiting":
        downloadsCount = this._privateInProgressDownloads.size;
        this._confirmCancelDownloads(
          aSubject,
          downloadsCount,
          "ON_LEAVE_PRIVATE_BROWSING"
        );
        break;
      case "last-pb-context-exited": {
        let collector;
        try {
          collector = aSubject?.QueryInterface(Ci.nsIPBMCleanupCollector);
        } catch (e) {}
        let cb = collector?.addPendingCleanup();

        let promise = (async function () {
          let list = await Downloads.getList(Downloads.PRIVATE);
          let downloads = await list.getAll();

          // Remove all downloads from list immediately, then finalize async.
          downloads.forEach(d => list.remove(d));
          await Promise.all(
            downloads.map(d => d.finalize(true).catch(console.error))
          );
        })();

        promise.then(
          () => cb?.complete(Cr.NS_OK),
          () => cb?.complete(Cr.NS_ERROR_FAILURE)
        );

        // Handle test mode
        if (lazy.gCombinedDownloadIntegration._testResolveClearPrivateList) {
          lazy.gCombinedDownloadIntegration._testResolveClearPrivateList(
            promise
          );
        } else {
          promise.catch(ex => console.error(ex));
        }
        break;
      }
      // While downloads are in progress we hold a wakelock asking the OS
      // not to enter automatic sleep. The wakelock is advisory: the OS may
      // still sleep on explicit user request (e.g. sleep menu, lid close)
      // or for other reasons (low battery, thermal). In the cases where the
      // sleep notification still fires, we cancel the active downloads so they
      // can be resumed on wake.
      case "sleep_notification":
      case "suspend_process_notification":
      case "network:offline-about-to-go-offline":
        // Ignore shutdown notification so aborted downloads will be restarted
        // on the next session.
        if (
          Services.startup.isInOrBeyondShutdownPhase(
            Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
          )
        ) {
          break;
        }
        for (let download of this._publicInProgressDownloads) {
          download.cancel();
          this._canceledOfflineDownloads.add(download);
        }
        for (let download of this._privateInProgressDownloads) {
          download.cancel();
          this._canceledOfflineDownloads.add(download);
        }
        break;
      case "wake_notification":
      case "resume_process_notification": {
        let wakeDelay = Services.prefs.getIntPref(
          "browser.download.manager.resumeOnWakeDelay",
          10000
        );

        if (wakeDelay >= 0) {
          this._wakeTimer = new Timer(
            this._resumeOfflineDownloads.bind(this),
            wakeDelay,
            Ci.nsITimer.TYPE_ONE_SHOT
          );
        }
        break;
      }
      case "network:offline-status-changed":
        if (aData == "online") {
          this._resumeOfflineDownloads();
        }
        break;
      // We need to unregister observers explicitly before we reach the
      // "xpcom-shutdown" phase, otherwise observers may be notified when some
      // required services are not available anymore. We can't unregister
      // observers on "quit-application", because this module is also loaded
      // during "make package" automation, and the quit notification is not sent
      // in that execution environment (bug 973637).
      case "xpcom-will-shutdown":
        for (let topic of kObserverTopics) {
          Services.obs.removeObserver(this, topic);
        }
        break;
      case "blocked-automatic-download":
        if (AppConstants.MOZ_BUILD_APP == "browser") {
          DownloadIntegration._initializeDownloadSpamProtection();
          DownloadIntegration.downloadSpamProtection.update(
            aData,
            aSubject.topChromeWindow
          );
        }
        break;
    }
  },

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
};

/**
 * Registers a Places observer so that operations on download history are
 * reflected on the provided list of downloads.
 *
 * You do not need to keep a reference to this object in order to keep it alive,
 * because the history service already keeps a strong reference to it.
 *
 * @param aList
 *        DownloadList object linked to this observer.
 */
var DownloadHistoryObserver = function (aList) {
  this._list = aList;

  this._placesObserver = new PlacesWeakCallbackWrapper(
    this.handlePlacesEvents.bind(this)
  );
  PlacesObservers.addListener(
    ["history-cleared", "page-removed"],
    this._placesObserver
  );
};

DownloadHistoryObserver.prototype = {
  /**
   * DownloadList object linked to this observer.
   */
  _list: null,

  handlePlacesEvents(events) {
    for (const event of events) {
      switch (event.type) {
        case "history-cleared": {
          this._list.removeFinished();
          break;
        }
        case "page-removed": {
          if (event.isRemovedFromStore) {
            this._list.removeFinished(
              download => event.url === download.source.url
            );
          }
          break;
        }
      }
    }
  },
};

/**
 * This view can be added to a DownloadList object to trigger a save operation
 * in the given DownloadStore object when a relevant change occurs.  You should
 * call the "initialize" method in order to register the view and load the
 * current state from disk.
 *
 * You do not need to keep a reference to this object in order to keep it alive,
 * because the DownloadList object already keeps a strong reference to it.
 *
 * @param aList
 *        The DownloadList object on which the view should be registered.
 * @param aStore
 *        The DownloadStore object used for saving.
 */
var DownloadAutoSaveView = function (aList, aStore) {
  this._list = aList;
  this._store = aStore;
  this._downloadsMap = new Map();
  this._writer = new lazy.DeferredTask(() => this._store.save(), kSaveDelayMs);
  lazy.AsyncShutdown.profileBeforeChange.addBlocker(
    "DownloadAutoSaveView: writing data",
    () => this._writer.finalize()
  );
};

DownloadAutoSaveView.prototype = {
  /**
   * DownloadList object linked to this view.
   */
  _list: null,

  /**
   * The DownloadStore object used for saving.
   */
  _store: null,

  /**
   * True when the initial state of the downloads has been loaded.
   */
  _initialized: false,

  /**
   * Registers the view and loads the current state from disk.
   */
  initialize() {
    // We set _initialized to true after adding the view, so that
    // onDownloadAdded doesn't cause a save to occur.
    this._list.addView(this);
    this._initialized = true;
  },

  /**
   * This map contains only Download objects that should be saved to disk, and
   * associates them with the result of their getSerializationHash function, for
   * the purpose of detecting changes to the relevant properties.
   */
  _downloadsMap: null,

  /**
   * DeferredTask for the save operation.
   */
  _writer: null,

  /**
   * Called when the list of downloads changed, this triggers the asynchronous
   * serialization of the list of downloads.
   */
  saveSoon() {
    this._writer.arm();
  },

  // DownloadList callback
  onDownloadAdded(aDownload) {
    if (lazy.gCombinedDownloadIntegration.shouldPersistDownload(aDownload)) {
      this._downloadsMap.set(aDownload, aDownload.getSerializationHash());
      if (this._initialized) {
        this.saveSoon();
      }
    }
  },

  // DownloadList callback
  onDownloadChanged(aDownload) {
    if (!lazy.gCombinedDownloadIntegration.shouldPersistDownload(aDownload)) {
      if (this._downloadsMap.has(aDownload)) {
        this._downloadsMap.delete(aDownload);
        this.saveSoon();
      }
      return;
    }

    let hash = aDownload.getSerializationHash();
    if (this._downloadsMap.get(aDownload) != hash) {
      this._downloadsMap.set(aDownload, hash);
      this.saveSoon();
    }
  },

  // DownloadList callback
  onDownloadRemoved(aDownload) {
    if (this._downloadsMap.has(aDownload)) {
      this._downloadsMap.delete(aDownload);
      this.saveSoon();
    }
  },
};
