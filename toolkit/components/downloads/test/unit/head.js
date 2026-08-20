/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Provides infrastructure for automated download components tests.
 */

"use strict";

var { Integration } = ChromeUtils.importESModule(
  "resource://gre/modules/Integration.sys.mjs"
);
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  FileTestUtils: "resource://testing-common/FileTestUtils.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  HttpServer: "resource://testing-common/httpd.sys.mjs",
  MockRegistrar: "resource://testing-common/MockRegistrar.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gExternalHelperAppService",
  "@mozilla.org/uriloader/external-helper-app-service;1",
  Ci.nsIExternalHelperAppService
);

/* global DownloadIntegration */
Integration.downloads.defineESModuleGetter(
  this,
  "DownloadIntegration",
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);

const ServerSocket = Components.Constructor(
  "@mozilla.org/network/server-socket;1",
  "nsIServerSocket",
  "init"
);
const BinaryOutputStream = Components.Constructor(
  "@mozilla.org/binaryoutputstream;1",
  "nsIBinaryOutputStream",
  "setOutputStream"
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gMIMEService",
  "@mozilla.org/mime;1",
  Ci.nsIMIMEService
);

const ReferrerInfo = Components.Constructor(
  "@mozilla.org/referrer-info;1",
  "nsIReferrerInfo",
  "init"
);

const TEST_TARGET_FILE_NAME = "test-download.txt";
const TEST_STORE_FILE_NAME = "test-downloads.json";

// We are testing an HTTPS referrer with HTTP downloads in order to verify that
// the default policy will not prevent the referrer from being passed around.
const TEST_REFERRER_URL = "https://www.example.com/referrer.html";

const TEST_DATA_SHORT = "This test string is downloaded.";
// Generate using gzipCompressString in TelemetryController.sys.mjs.
const TEST_DATA_SHORT_GZIP_ENCODED_FIRST = [
  31, 139, 8, 0, 0, 0, 0, 0, 0, 3, 11, 201, 200, 44, 86, 40, 73, 45, 46, 81, 40,
  46, 41, 202, 204,
];
const TEST_DATA_SHORT_GZIP_ENCODED_SECOND = [
  75, 87, 0, 114, 83, 242, 203, 243, 114, 242, 19, 83, 82, 83, 244, 0, 151, 222,
  109, 43, 31, 0, 0, 0,
];
const TEST_DATA_SHORT_GZIP_ENCODED = TEST_DATA_SHORT_GZIP_ENCODED_FIRST.concat(
  TEST_DATA_SHORT_GZIP_ENCODED_SECOND
);

/**
 * All the tests are implemented with add_task, this starts them automatically.
 */
function run_test() {
  do_get_profile();
  run_next_test();
}

// Support functions

/**
 * HttpServer object initialized before tests start.
 */
var gHttpServer;

/**
 * Given a file name, returns a string containing an URI that points to the file
 * on the currently running instance of the test HTTP server.
 */
function httpUrl(aFileName) {
  return (
    "http://www.example.com:" +
    gHttpServer.identity.primaryPort +
    "/" +
    aFileName
  );
}

/**
 * Returns a reference to a temporary file that is guaranteed not to exist and
 * is cleaned up later. See FileTestUtils.getTempFile for details.
 */
function getTempFile(leafName) {
  return FileTestUtils.getTempFile(leafName);
}

/**
 * Check for file existence.
 *
 * @param {string} path The file path.
 */
async function fileExists(path) {
  try {
    return (await IOUtils.stat(path)).type == "regular";
  } catch (ex) {
    return false;
  }
}

/**
 * Waits for pending events to be processed.
 *
 * @returns {Promise<void>}
 *   Resolves when pending events have been processed.
 * @rejects Never.
 */
function promiseExecuteSoon() {
  return new Promise(resolve => {
    executeSoon(resolve);
  });
}

/**
 * Waits for a pending events to be processed after a timeout.
 *
 * @returns {Promise<void>}
 *   Resolves when pending events have been processed.
 * @rejects Never.
 */
function promiseTimeout(aTime) {
  return new Promise(resolve => {
    do_timeout(aTime, resolve);
  });
}

/**
 * Waits for a new history visit to be notified for the specified URI.
 *
 * @param aUrl
 *        String containing the URI that will be visited.
 *
 * @returns {Promise}
 *   Resolves to an Array [aTime, aTransitionType] from page-visited places event.
 * @rejects Never.
 */
function promiseWaitForVisit(aUrl) {
  return new Promise(resolve => {
    function listener(aEvents) {
      Assert.equal(aEvents.length, 1);
      let event = aEvents[0];
      Assert.equal(event.type, "page-visited");
      if (event.url == aUrl) {
        PlacesObservers.removeListener(["page-visited"], listener);
        resolve([event.visitTime, event.transitionType, event.lastKnownTitle]);
      }
    }
    PlacesObservers.addListener(["page-visited"], listener);
  });
}

/**
 * Creates a new Download object, setting a temporary file as the target.
 *
 * @param aSourceUrl
 *        String containing the URI for the download source, or null to use
 *        httpUrl("source.txt").
 *
 * @returns {Promise}
 *   Resolves to the newly created Download object.
 * @rejects JavaScript exception.
 */
function promiseNewDownload(aSourceUrl) {
  return Downloads.createDownload({
    source: aSourceUrl || httpUrl("source.txt"),
    target: getTempFile(TEST_TARGET_FILE_NAME),
  });
}

/**
 * Starts a new download using the nsIWebBrowserPersist interface, and controls
 * it using the legacy nsITransfer interface.
 *
 * @param aSourceUrl
 *        String containing the URI for the download source, or null to use
 *        httpUrl("source.txt").
 * @param aOptions
 *        An optional object used to control the behavior of this function.
 *        You may pass an object with a subset of the following fields:
 *        {
 *          isPrivate: Boolean indicating whether the download originated from a
 *                     private window.
 *          referrerInfo: referrerInfo for the download source.
 *          cookieJarSettings: cookieJarSettings for the download source.
 *          targetFile: nsIFile for the target, or null to use a temporary file.
 *          outPersist: Receives a reference to the created nsIWebBrowserPersist
 *                      instance.
 *          launchWhenSucceeded: Boolean indicating whether the target should
 *                               be launched when it has completed successfully.
 *          launcherPath: String containing the path of the custom executable to
 *                        use to launch the target of the download.
 *        }
 *
 * @returns {Promise}
 *   Resolves to the Download object created as a consequence of controlling the
 *   download through the legacy nsITransfer interface.
 * @rejects Never. The current test fails in case of exceptions.
 */
function promiseStartLegacyDownload(aSourceUrl, aOptions) {
  let sourceURI = NetUtil.newURI(aSourceUrl || httpUrl("source.txt"));
  let targetFile =
    (aOptions && aOptions.targetFile) || getTempFile(TEST_TARGET_FILE_NAME);

  let persist = Cc[
    "@mozilla.org/embedding/browser/nsWebBrowserPersist;1"
  ].createInstance(Ci.nsIWebBrowserPersist);
  if (aOptions) {
    aOptions.outPersist = persist;
  }

  let fileExtension = null,
    mimeInfo = null;
  let match = sourceURI.pathQueryRef.match(/\.([^.\/]+)$/);
  if (match) {
    fileExtension = match[1];
  }

  if (fileExtension) {
    try {
      mimeInfo = gMIMEService.getFromTypeAndExtension(null, fileExtension);
      mimeInfo.preferredAction = Ci.nsIMIMEInfo.saveToDisk;
    } catch (ex) {}
  }

  if (aOptions && aOptions.launcherPath) {
    Assert.notEqual(mimeInfo, null);

    let localHandlerApp = Cc[
      "@mozilla.org/uriloader/local-handler-app;1"
    ].createInstance(Ci.nsILocalHandlerApp);
    localHandlerApp.executable = new FileUtils.File(aOptions.launcherPath);

    mimeInfo.preferredApplicationHandler = localHandlerApp;
    mimeInfo.preferredAction = Ci.nsIMIMEInfo.useHelperApp;
  }
  if (aOptions && aOptions.launcherId) {
    Assert.notEqual(mimeInfo, null);

    let gioHandlerApp = Cc["@mozilla.org/gio-service;1"]
      .getService(Ci.nsIGIOService)
      .createHandlerAppFromAppId(aOptions.launcherId);

    mimeInfo.preferredApplicationHandler = gioHandlerApp;
    mimeInfo.preferredAction = Ci.nsIMIMEInfo.useHelperApp;
  }

  if (aOptions && aOptions.launchWhenSucceeded) {
    Assert.notEqual(mimeInfo, null);

    mimeInfo.preferredAction = Ci.nsIMIMEInfo.useHelperApp;
  }

  // Apply decoding if required by the "Content-Encoding" header.
  persist.persistFlags &= ~Ci.nsIWebBrowserPersist.PERSIST_FLAGS_NO_CONVERSION;
  persist.persistFlags |=
    Ci.nsIWebBrowserPersist.PERSIST_FLAGS_AUTODETECT_APPLY_CONVERSION;

  let transfer = Cc["@mozilla.org/transfer;1"].createInstance(Ci.nsITransfer);

  return new Promise(resolve => {
    Downloads.getList(Downloads.ALL)
      .then(function (aList) {
        // Temporarily register a view that will get notified when the download we
        // are controlling becomes visible in the list of downloads.
        aList.addView({
          onDownloadAdded(aDownload) {
            aList.removeView(this);

            // Remove the download to keep the list empty for the next test.  This
            // also allows the caller to register the "onchange" event directly.
            let promise = aList.remove(aDownload);

            // When the download object is ready, make it available to the caller.
            promise.then(
              () => resolve(aDownload),
              do_report_unexpected_exception
            );
          },
        });

        let isPrivate = aOptions && aOptions.isPrivate;
        let referrerInfo = aOptions ? aOptions.referrerInfo : null;
        let cookieJarSettings = aOptions ? aOptions.cookieJarSettings : null;
        let classification =
          aOptions?.downloadClassification ??
          Ci.nsITransfer.DOWNLOAD_ACCEPTABLE;
        // Initialize the components so they reference each other.  This will cause
        // the Download object to be created and added to the public downloads.
        transfer.init(
          sourceURI,
          aOptions?.originalUrl ? NetUtil.newURI(aOptions.originalUrl) : null,
          NetUtil.newURI(targetFile),
          null,
          mimeInfo,
          null,
          null,
          persist,
          isPrivate,
          classification,
          null
        );
        persist.progressListener = transfer;

        // Start the actual download process.
        persist.saveURI(
          sourceURI,
          Services.scriptSecurityManager.getSystemPrincipal(),
          0,
          referrerInfo,
          cookieJarSettings,
          null,
          null,
          targetFile,
          Ci.nsIContentPolicy.TYPE_SAVEAS_DOWNLOAD,
          isPrivate
        );
      })
      .catch(do_report_unexpected_exception);
  });
}

/**
 * Starts a new download using the nsIHelperAppService interface, and controls
 * it using the legacy nsITransfer interface.  The source of the download will
 * be "interruptible_resumable.txt" and partially downloaded data will be kept.
 *
 * @param aSourceUrl
 *        String containing the URI for the download source, or null to use
 *        httpUrl("interruptible_resumable.txt").
 *
 * @returns {Promise}
 *   Resolves to rhe Download object created as a consequence of controlling the
 *   download through the legacy nsITransfer interface.
 * @rejects Never. The current test fails in case of exceptions.
 */
function promiseStartExternalHelperAppServiceDownload(aSourceUrl) {
  let sourceURI = NetUtil.newURI(
    aSourceUrl || httpUrl("interruptible_resumable.txt")
  );

  return new Promise(resolve => {
    Downloads.getList(Downloads.PUBLIC)
      .then(function (aList) {
        // Temporarily register a view that will get notified when the download we
        // are controlling becomes visible in the list of downloads.
        aList.addView({
          onDownloadAdded(aDownload) {
            aList.removeView(this);

            // Remove the download to keep the list empty for the next test.  This
            // also allows the caller to register the "onchange" event directly.
            let promise = aList.remove(aDownload);

            // When the download object is ready, make it available to the caller.
            promise.then(
              () => resolve(aDownload),
              do_report_unexpected_exception
            );
          },
        });

        let channel = NetUtil.newChannel({
          uri: sourceURI,
          loadUsingSystemPrincipal: true,
        });

        // Start the actual download process.
        channel.asyncOpen({
          contentListener: null,

          onStartRequest(aRequest) {
            let requestChannel = aRequest.QueryInterface(Ci.nsIChannel);
            this.contentListener = gExternalHelperAppService.doContent(
              requestChannel.contentType,
              aRequest,
              null,
              true
            );
            this.contentListener.onStartRequest(aRequest);
          },

          onStopRequest(aRequest, aStatusCode) {
            this.contentListener.onStopRequest(aRequest, aStatusCode);
          },

          onDataAvailable(aRequest, aInputStream, aOffset, aCount) {
            this.contentListener.onDataAvailable(
              aRequest,
              aInputStream,
              aOffset,
              aCount
            );
          },
        });
      })
      .catch(do_report_unexpected_exception);
  });
}

/**
 * Waits for a download to reach a specified progress value. If not
 * specified, waits until 50% is reached.
 *
 * @param aDownload
 *        The Download object to wait upon.
 * @param aProgress
 *        The progress that should be reached.
 *
 * @returns {Promise<void>}
 *   Resolves when the download has reached half of its progress.
 * @rejects Never.
 */
function promiseDownloadMidway(aDownload, aProgress = 50) {
  return new Promise(resolve => {
    // Wait for the download to reach half of its progress.
    let onchange = function () {
      if (
        !aDownload.stopped &&
        !aDownload.canceled &&
        aDownload.progress == aProgress
      ) {
        aDownload.onchange = null;
        resolve();
      }
    };

    // Register for the notification, but also call the function directly in
    // case the download already reached the expected progress.
    aDownload.onchange = onchange;
    onchange();
  });
}

/**
 * Waits for a download to make any amount of progress.
 *
 * @param aDownload
 *        The Download object to wait upon.
 *
 * @returns {Promise<void>}
 *   Resolves when the download has transfered any number of bytes.
 * @rejects Never.
 */
function promiseDownloadStarted(aDownload) {
  return new Promise(resolve => {
    // Wait for the download to transfer some amount of bytes.
    let onchange = function () {
      if (
        !aDownload.stopped &&
        !aDownload.canceled &&
        aDownload.currentBytes > 0
      ) {
        aDownload.onchange = null;
        resolve();
      }
    };

    // Register for the notification, but also call the function directly in
    // case the download already reached the expected progress.
    aDownload.onchange = onchange;
    onchange();
  });
}

/**
 * Waits for a download to finish.
 *
 * @param aDownload
 *        The Download object to wait upon.
 *
 * @returns {Promise<void>}
 *   Resolves when the download succeeded or errored.
 * @rejects Never.
 */
function promiseDownloadFinished(aDownload) {
  return new Promise(resolve => {
    // Wait for the download to finish.
    let onchange = function () {
      if (aDownload.succeeded || aDownload.error) {
        aDownload.onchange = null;
        resolve();
      }
    };

    // Register for the notification, but also call the function directly in
    // case the download already reached the expected progress.
    aDownload.onchange = onchange;
    onchange();
  });
}

/**
 * Waits for a download to finish, in case it has not finished already.
 *
 * @param aDownload
 *        The Download object to wait upon.
 *
 * @returns {Promise<void>}
 *   Resolves when the download has finished successfully.
 * @rejects JavaScript exception if the download failed.
 */
function promiseDownloadStopped(aDownload) {
  if (!aDownload.stopped) {
    // The download is in progress, wait for the current attempt to finish and
    // report any errors that may occur.
    return aDownload.start();
  }

  if (aDownload.succeeded) {
    return Promise.resolve();
  }

  // The download failed or was canceled.
  return Promise.reject(aDownload.error || new Error("Download canceled."));
}

/**
 * Returns a new public or private DownloadList object.
 *
 * @param aIsPrivate
 *        True for the private list, false or undefined for the public list.
 *
 * @returns {Promise}
 *   Resolves to the newly created DownloadList object.
 * @rejects JavaScript exception.
 */
function promiseNewList(aIsPrivate) {
  // We need to clear all the internal state for the list and summary objects,
  // since all the objects are interdependent internally.
  Downloads._promiseListsInitialized = null;
  Downloads._lists = {};
  Downloads._summaries = {};

  return Downloads.getList(aIsPrivate ? Downloads.PRIVATE : Downloads.PUBLIC);
}

/**
 * Ensures that the given file contents are equal to the given string.
 *
 * @param aPath
 *        String containing the path of the file whose contents should be
 *        verified.
 * @param aExpectedContents
 *        String containing the octets that are expected in the file.
 *
 * @returns {Promise<void>}
 *   Resolves when the operation completes.
 * @rejects Never.
 */
async function promiseVerifyContents(aPath, aExpectedContents) {
  let file = new FileUtils.File(aPath);

  if (!(await IOUtils.exists(aPath))) {
    do_throw("File does not exist: " + aPath);
  }

  if ((await IOUtils.stat(aPath)).size == 0) {
    do_throw("File is empty: " + aPath);
  }

  await new Promise(resolve => {
    NetUtil.asyncFetch(
      { uri: NetUtil.newURI(file), loadUsingSystemPrincipal: true },
      function (aInputStream, aStatus) {
        Assert.ok(Components.isSuccessCode(aStatus));
        let contents = NetUtil.readInputStreamToString(
          aInputStream,
          aInputStream.available()
        );
        if (
          contents.length > TEST_DATA_SHORT.length * 2 ||
          /[^\x20-\x7E]/.test(contents)
        ) {
          // Do not print the entire content string to the test log.
          Assert.equal(contents.length, aExpectedContents.length);
          Assert.equal(contents, aExpectedContents);
        } else {
          // Print the string if it is short and made of printable characters.
          Assert.equal(contents, aExpectedContents);
        }
        resolve();
      }
    );
  });
}

/**
 * Creates and starts a new download, configured to keep partial data, and
 * returns only when the first part of "interruptible_resumable.txt" has been
 * saved to disk.  You must call "continueResponses" to allow the interruptible
 * request to continue.
 *
 * This function uses either DownloadCopySaver or DownloadLegacySaver based on
 * the current test run.
 *
 * @param aOptions
 *        An optional object used to control the behavior of this function.
 *        You may pass an object with a subset of the following fields:
 *        {
 *          useLegacySaver: Boolean indicating whether to launch a legacy download.
 *        }
 *
 * @returns {Promise}
 *   Resolves to the newly created Download object, still in progress.
 * @rejects JavaScript exception.
 */
async function promiseStartDownload_tryToKeepPartialData({
  useLegacySaver = false,
} = {}) {
  mustInterruptResponses();

  // Start a new download and configure it to keep partially downloaded data.
  let download;
  if (!useLegacySaver) {
    let targetFilePath = getTempFile(TEST_TARGET_FILE_NAME).path;
    download = await Downloads.createDownload({
      source: httpUrl("interruptible_resumable.txt"),
      target: {
        path: targetFilePath,
        partFilePath: targetFilePath + ".part",
      },
    });
    download.tryToKeepPartialData = true;
    download.start().catch(() => {});
  } else {
    // Start a download using nsIExternalHelperAppService, that is configured
    // to keep partially downloaded data by default.
    download = await promiseStartExternalHelperAppServiceDownload();
  }

  await promiseDownloadMidway(download);
  await promisePartFileReady(download);

  return download;
}

/**
 * This function should be called after the progress notification for a download
 * is received, and waits for the worker thread of BackgroundFileSaver to
 * receive the data to be written to the ".part" file on disk.
 *
 * @returns {Promise<void>}
 *   Resolves when the ".part" file has been written to disk.
 * @rejects JavaScript exception.
 */
async function promisePartFileReady(aDownload) {
  // We don't have control over the file output code in BackgroundFileSaver.
  // After we receive the download progress notification, we may only check
  // that the ".part" file has been created, while its size cannot be
  // determined because the file is currently open.
  try {
    do {
      await promiseTimeout(50);
    } while (!(await IOUtils.exists(aDownload.target.partFilePath)));
  } catch (ex) {
    if (!DOMException.isInstance(ex)) {
      throw ex;
    }
    // This indicates that the file has been created and cannot be accessed.
    // The specific error might vary with the platform.
    info("IOUtils exception while checking existence: " + ex.name);
    // Wait some more time to allow the write to complete.
    await promiseTimeout(100);
  }
}

/**
 * Create a download which will be reputation blocked.
 *
 * @param options
 *        {
 *           keepPartialData: bool,
 *           keepBlockedData: bool,
 *           useLegacySaver: bool,
 *           verdict: nsIApplicationReputationService value indicating the reason for the block,
 *           expectedError: Downloads.Error value indicating the expected error,
 *        }
 * @returns {Promise}
 *   Resolves when the reputation blocked download.
 * @rejects JavaScript exception.
 */
async function promiseBlockedDownload({
  keepPartialData,
  keepBlockedData,
  useLegacySaver,
  verdict = Ci.nsIApplicationReputationService.VERDICT_UNCOMMON,
  expectedError = Downloads.Error.BLOCK_VERDICT_UNCOMMON,
} = {}) {
  let blockFn = () => ({
    shouldBlockForReputationCheck: () =>
      Promise.resolve({
        shouldBlock: true,
        verdict,
      }),
    shouldKeepBlockedData: () => Promise.resolve(keepBlockedData),
  });

  Integration.downloads.register(blockFn);
  function cleanup() {
    Integration.downloads.unregister(blockFn);
  }
  registerCleanupFunction(cleanup);

  let download;

  try {
    if (keepPartialData) {
      download = await promiseStartDownload_tryToKeepPartialData({
        useLegacySaver,
      });
      continueResponses();
    } else if (useLegacySaver) {
      download = await promiseStartLegacyDownload();
    } else {
      download = await promiseNewDownload();
      await download.start();
      do_throw("The download should have blocked.");
    }

    await promiseDownloadStopped(download);
    do_throw("The download should have blocked.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error) || !ex.becauseBlocked) {
      throw ex;
    }
    Assert.ok(ex.becauseBlockedByReputationCheck);
    Assert.equal(ex.reputationCheckVerdict, expectedError);
    Assert.ok(download.error.becauseBlockedByReputationCheck);
    Assert.equal(download.error.reputationCheckVerdict, expectedError);
  }

  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);

  cleanup();
  return download;
}

/**
 * Starts a socket listener that closes each incoming connection.
 *
 * @returns nsIServerSocket that listens for connections.  Call its "close"
 *          method to stop listening and free the server port.
 */
function startFakeServer() {
  let serverSocket = new ServerSocket(-1, true, -1);
  serverSocket.asyncListen({
    onSocketAccepted(aServ, aTransport) {
      aTransport.close(Cr.NS_BINDING_ABORTED);
    },
    onStopListening() {},
  });
  return serverSocket;
}

/**
 * This is an internal reference that should not be used directly by tests.
 */
var _gDeferResponses = Promise.withResolvers();

/**
 * Ensures that all the interruptible requests started after this function is
 * called won't complete until the continueResponses function is called.
 *
 * Normally, the internal HTTP server returns all the available data as soon as
 * a request is received.  In order for some requests to be served one part at a
 * time, special interruptible handlers are registered on the HTTP server.  This
 * allows testing events or actions that need to happen in the middle of a
 * download.
 *
 * For example, the handler accessible at the httpUri("interruptible.txt")
 * address returns the TEST_DATA_SHORT text, then it may block until the
 * continueResponses method is called.  At this point, the handler sends the
 * TEST_DATA_SHORT text again to complete the response.
 *
 * If an interruptible request is started before the function is called, it may
 * or may not be blocked depending on the actual sequence of events.
 */
function mustInterruptResponses() {
  // If there are pending blocked requests, allow them to complete.  This is
  // done to prevent requests from being blocked forever, but should not affect
  // the test logic, since previously started requests should not be monitored
  // on the client side anymore.
  _gDeferResponses.resolve();

  info("Interruptible responses will be blocked midway.");
  _gDeferResponses = Promise.withResolvers();
}

/**
 * Allows all the current and future interruptible requests to complete.
 */
function continueResponses() {
  info("Interruptible responses are now allowed to continue.");
  _gDeferResponses.resolve();
}

/**
 * Registers an interruptible response handler.
 *
 * @param aPath
 *        Path passed to nsIHttpServer.registerPathHandler.
 * @param aFirstPartFn
 *        This function is called when the response is received, with the
 *        aRequest and aResponse arguments of the server.
 * @param aSecondPartFn
 *        This function is called with the aRequest and aResponse arguments of
 *        the server, when the continueResponses function is called.
 */
function registerInterruptibleHandler(aPath, aFirstPartFn, aSecondPartFn) {
  gHttpServer.registerPathHandler(aPath, function (aRequest, aResponse) {
    info("Interruptible request started.");

    // Process the first part of the response.
    aResponse.processAsync();
    aFirstPartFn(aRequest, aResponse);

    // Wait on the current deferred object, then finish the request.
    _gDeferResponses.promise
      .then(function RIH_onSuccess() {
        aSecondPartFn(aRequest, aResponse);
        aResponse.finish();
        info("Interruptible request finished.");
      })
      .catch(console.error);
  });
}

/**
 * Ensure the given date object is valid.
 *
 * @param aDate
 *        The date object to be checked. This value can be null.
 */
function isValidDate(aDate) {
  return aDate && aDate.getTime && !isNaN(aDate.getTime());
}

/**
 * Check actual ReferrerInfo is the same as expected.
 * Because the actual download's referrer info's computedReferrer is computed
 * from referrerPolicy and originalReferrer and is non-null, and the expected
 * referrer info was constructed in isolation and therefore the computedReferrer
 * is null, it isn't possible to use equals here.
 */
function checkEqualReferrerInfos(aActualInfo, aExpectedInfo) {
  Assert.equal(
    !!aExpectedInfo.originalReferrer,
    !!aActualInfo.originalReferrer
  );
  if (aExpectedInfo.originalReferrer && aActualInfo.originalReferrer) {
    Assert.equal(
      aExpectedInfo.originalReferrer.spec,
      aActualInfo.originalReferrer.spec
    );
  }

  Assert.equal(aExpectedInfo.sendReferrer, aActualInfo.sendReferrer);
  Assert.equal(aExpectedInfo.referrerPolicy, aActualInfo.referrerPolicy);
}

/**
 * Waits for the download annotations to be set for the given page, required
 * because the addDownload method will add these to the database asynchronously.
 */
function waitForAnnotation(sourceUriSpec, annotationName, optionalValue) {
  return TestUtils.waitForCondition(async () => {
    let pageInfo = await PlacesUtils.history.fetch(sourceUriSpec, {
      includeAnnotations: true,
    });
    if (optionalValue) {
      return pageInfo?.annotations.get(annotationName) == optionalValue;
    }
    return pageInfo?.annotations.has(annotationName);
  }, `Should have found annotation ${annotationName} for ${sourceUriSpec}`);
}

/**
 * Position of the first byte served by the "interruptible_resumable.txt"
 * handler during the most recent response.
 */
var gMostRecentFirstBytePos;

// Initialization functions common to all tests

add_setup(function test_common_initialize() {
  // Start the HTTP server.
  gHttpServer = new HttpServer();
  gHttpServer.registerDirectory("/", do_get_file("../data"));
  gHttpServer.start(-1);
  registerCleanupFunction(() => {
    return new Promise(resolve => {
      // Ensure all the pending HTTP requests have a chance to finish.
      continueResponses();
      // Stop the HTTP server, calling resolve when it's done.
      gHttpServer.stop(resolve);
    });
  });

  // Serve the downloads from a domain located in the Internet zone on Windows.
  gHttpServer.identity.setPrimary(
    "http",
    "www.example.com",
    gHttpServer.identity.primaryPort
  );
  Services.prefs.setCharPref("network.dns.localDomains", "www.example.com");
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("network.dns.localDomains");
  });

  // Cache locks might prevent concurrent requests to the same resource, and
  // this may block tests that use the interruptible handlers.
  Services.prefs.setBoolPref("browser.cache.disk.enable", false);
  Services.prefs.setBoolPref("browser.cache.memory.enable", false);
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("browser.cache.disk.enable");
    Services.prefs.clearUserPref("browser.cache.memory.enable");
  });

  // Allow relaxing default referrer.
  Services.prefs.setBoolPref(
    "network.http.referer.disallowCrossSiteRelaxingDefault",
    false
  );
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref(
      "network.http.referer.disallowCrossSiteRelaxingDefault"
    );
  });

  registerInterruptibleHandler(
    "/interruptible.txt",
    function firstPart(aRequest, aResponse) {
      aResponse.setHeader("Content-Type", "text/plain", false);
      aResponse.setHeader(
        "Content-Length",
        "" + TEST_DATA_SHORT.length * 2,
        false
      );
      aResponse.write(TEST_DATA_SHORT);
    },
    function secondPart(aRequest, aResponse) {
      aResponse.write(TEST_DATA_SHORT);
    }
  );

  registerInterruptibleHandler(
    "/interruptible_nosize.txt",
    function firstPart(aRequest, aResponse) {
      aResponse.setHeader("Content-Type", "text/plain", false);
      aResponse.write(TEST_DATA_SHORT);
    },
    function secondPart(aRequest, aResponse) {
      aResponse.write(TEST_DATA_SHORT);
    }
  );

  registerInterruptibleHandler(
    "/interruptible_resumable.txt",
    function firstPart(aRequest, aResponse) {
      aResponse.setHeader("Content-Type", "text/plain", false);

      // Determine if only part of the data should be sent.
      let data = TEST_DATA_SHORT + TEST_DATA_SHORT;
      if (aRequest.hasHeader("Range")) {
        var matches = aRequest
          .getHeader("Range")
          .match(/^\s*bytes=(\d+)?-(\d+)?\s*$/);
        var firstBytePos = matches[1] === undefined ? 0 : matches[1];
        var lastBytePos =
          matches[2] === undefined ? data.length - 1 : matches[2];
        if (firstBytePos >= data.length) {
          aResponse.setStatusLine(
            aRequest.httpVersion,
            416,
            "Requested Range Not Satisfiable"
          );
          aResponse.setHeader("Content-Range", "*/" + data.length, false);
          aResponse.finish();
          return;
        }

        aResponse.setStatusLine(aRequest.httpVersion, 206, "Partial Content");
        aResponse.setHeader(
          "Content-Range",
          firstBytePos + "-" + lastBytePos + "/" + data.length,
          false
        );

        data = data.substring(firstBytePos, lastBytePos + 1);

        gMostRecentFirstBytePos = firstBytePos;
      } else {
        gMostRecentFirstBytePos = 0;
      }

      aResponse.setHeader("Content-Length", "" + data.length, false);

      aResponse.write(data.substring(0, data.length / 2));

      // Store the second part of the data on the response object, so that it
      // can be used by the secondPart function.
      aResponse.secondPartData = data.substring(data.length / 2);
    },
    function secondPart(aRequest, aResponse) {
      aResponse.write(aResponse.secondPartData);
    }
  );

  registerInterruptibleHandler(
    "/interruptible_gzip.txt",
    function firstPart(aRequest, aResponse) {
      aResponse.setHeader("Content-Type", "text/plain", false);
      aResponse.setHeader("Content-Encoding", "gzip", false);
      aResponse.setHeader(
        "Content-Length",
        "" + TEST_DATA_SHORT_GZIP_ENCODED.length
      );

      let bos = new BinaryOutputStream(aResponse.bodyOutputStream);
      bos.writeByteArray(TEST_DATA_SHORT_GZIP_ENCODED_FIRST);
    },
    function secondPart(aRequest, aResponse) {
      let bos = new BinaryOutputStream(aResponse.bodyOutputStream);
      bos.writeByteArray(TEST_DATA_SHORT_GZIP_ENCODED_SECOND);
    }
  );

  gHttpServer.registerPathHandler(
    "/shorter-than-content-length-http-1-1.txt",
    function (aRequest, aResponse) {
      aResponse.processAsync();
      aResponse.setStatusLine("1.1", 200, "OK");
      aResponse.setHeader("Content-Type", "text/plain", false);
      aResponse.setHeader(
        "Content-Length",
        "" + TEST_DATA_SHORT.length * 2,
        false
      );
      aResponse.write(TEST_DATA_SHORT);
      aResponse.finish();
    }
  );

  gHttpServer.registerPathHandler("/busy.txt", function (aRequest, aResponse) {
    aResponse.setStatusLine("1.1", 504, "Gateway Timeout");
    aResponse.setHeader("Content-Type", "text/plain", false);
    aResponse.setHeader("Content-Length", "" + TEST_DATA_SHORT.length, false);
    aResponse.write(TEST_DATA_SHORT);
  });

  gHttpServer.registerPathHandler("/redirect", function (aRequest, aResponse) {
    aResponse.setStatusLine("1.1", 301, "Moved Permanently");
    aResponse.setHeader("Location", httpUrl("busy.txt"), false);
    aResponse.setHeader("Content-Type", "text/javascript", false);
    aResponse.setHeader("Content-Length", "0", false);
  });

  // This URL will emulate being blocked by Windows Parental controls
  gHttpServer.registerPathHandler(
    "/parentalblocked.zip",
    function (aRequest, aResponse) {
      aResponse.setStatusLine(
        aRequest.httpVersion,
        450,
        "Blocked by Windows Parental Controls"
      );
    }
  );

  // This URL sends some data followed by an RST packet
  gHttpServer.registerPathHandler(
    "/netreset.txt",
    function (aRequest, aResponse) {
      info("Starting response that will be aborted.");
      aResponse.processAsync();
      aResponse.setHeader("Content-Type", "text/plain", false);
      aResponse.write(TEST_DATA_SHORT);
      promiseExecuteSoon()
        .then(() => {
          aResponse.abort(null, true);
          aResponse.finish();
          info("Aborting response with network reset.");
        })
        .then(null, console.error);
    }
  );

  // During unit tests, most of the functions that require profile access or
  // operating system features will be disabled. Individual tests may override
  // them again to check for specific behaviors.
  Integration.downloads.register(base => {
    let override = {
      loadPublicDownloadListFromStore: () => Promise.resolve(),
      shouldKeepBlockedData: () => Promise.resolve(false),
      shouldBlockForParentalControls: () => Promise.resolve(false),
      shouldBlockForReputationCheck: () =>
        Promise.resolve({
          shouldBlock: false,
          verdict: Ci.nsIApplicationReputationService.VERDICT_SAFE,
        }),
      confirmLaunchExecutable: () => Promise.resolve(),
      launchFile: () => Promise.resolve(),
      showContainingDirectory: () => Promise.resolve(),
      // This flag allows re-enabling the default observers during their tests.
      allowObservers: false,
      addListObservers() {
        return this.allowObservers
          ? super.addListObservers(...arguments)
          : Promise.resolve();
      },
      // This flag allows re-enabling the download directory logic for its tests.
      _allowDirectories: false,
      set allowDirectories(value) {
        this._allowDirectories = value;
        // We have to invalidate the previously computed directory path.
        this._downloadsDirectory = null;
      },
      _getDirectory(name) {
        return super._getDirectory(this._allowDirectories ? name : "TmpD");
      },
    };
    Object.setPrototypeOf(override, base);
    return override;
  });

  // Make sure that downloads started using nsIExternalHelperAppService are
  // saved to disk without asking for a destination interactively.
  let mock = {
    QueryInterface: ChromeUtils.generateQI(["nsIHelperAppLauncherDialog"]),
    promptForSaveToFileAsync(aLauncher) {
      // The dialog should create the empty placeholder file.
      let file = getTempFile(TEST_TARGET_FILE_NAME);
      file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
      aLauncher.saveDestinationAvailable(file);
    },
  };

  let cid = MockRegistrar.register(
    "@mozilla.org/helperapplauncherdialog;1",
    mock
  );
  registerCleanupFunction(() => {
    MockRegistrar.unregister(cid);
  });
});
