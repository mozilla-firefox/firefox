/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This script is loaded by "test_DownloadCore.js" and "test_DownloadLegacy.js"
 * with different values of the gUseLegacySaver variable, to apply tests to both
 * the "copy" and "legacy" saver implementations.
 */

/* import-globals-from head.js */
/* global gUseLegacySaver */

"use strict";

// Globals

const kDeleteTempFileOnExit = "browser.helperApps.deleteTempFileOnExit";
// TODO (bug 1986365): we should replace the setting of this pref with an actual
// test for the "new" behaviour (introduced in bug 1790641).
const kDeletePrivateFileFeature = "browser.download.enableDeletePrivate";
const kDecodeContentForKnownCompressedExtensions =
  "network.http.decode_content_for_known_compressed_extensions";

ChromeUtils.defineESModuleGetters(this, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

/**
 * Creates and starts a new download, using either DownloadCopySaver or
 * DownloadLegacySaver based on the current test run.
 *
 * @returns {Promise}
 *   Resolves to the newly created Download object. The download may be in
 *   progress or already finished. The promiseDownloadStopped function can be
 *   used to wait for completion.
 * @rejects JavaScript exception.
 */
function promiseStartDownload(aSourceUrl) {
  if (gUseLegacySaver) {
    return promiseStartLegacyDownload(aSourceUrl);
  }

  return promiseNewDownload(aSourceUrl).then(download => {
    download.start().catch(() => {});
    return download;
  });
}

/**
 * Checks that the actual data written to disk matches the expected data as well
 * as the properties of the given DownloadTarget object.
 *
 * @param downloadTarget
 *        The DownloadTarget object whose details have to be verified.
 * @param expectedContents
 *        String containing the octets that are expected in the file.
 *
 * @returns {Promise<void>}
 *   Resolves when the properties have been verified.
 * @rejects JavaScript exception.
 */
var promiseVerifyTarget = async function (downloadTarget, expectedContents) {
  Assert.ok(downloadTarget.exists);
  Assert.equal(
    await expectNonZeroDownloadTargetSize(downloadTarget),
    expectedContents.length
  );
  await promiseVerifyContents(downloadTarget.path, expectedContents);
};

/**
 * This is a temporary workaround for frequent intermittent Bug 1760112.
 * For some reason the download target size is not updated, even if the code
 * is "apparently" already executing and awaiting for refresh().
 * TODO(Bug 1814364): Figure out a proper fix for this.
 */
async function expectNonZeroDownloadTargetSize(downloadTarget) {
  todo_check_true(downloadTarget.size, "Size should not be zero.");
  if (!downloadTarget.size) {
    await downloadTarget.refresh();
  }
  return downloadTarget.size;
}

/**
 * Waits for an attempt to launch a file, and returns the nsIMIMEInfo used for
 * the launch, or null if the file was launched with the default handler.
 */
function waitForFileLaunched() {
  return new Promise(resolve => {
    let waitFn = () => ({
      launchFile(file, mimeInfo) {
        Integration.downloads.unregister(waitFn);
        if (
          !mimeInfo ||
          mimeInfo.preferredAction == Ci.nsIMIMEInfo.useSystemDefault
        ) {
          resolve(null);
        } else {
          resolve(mimeInfo);
        }
        return Promise.resolve();
      },
    });
    Integration.downloads.register(waitFn);
  });
}

/**
 * Waits for an attempt to show the directory where a file is located, and
 * returns the path of the file.
 */
function waitForDirectoryShown() {
  return new Promise(resolve => {
    let waitFn = () => ({
      showContainingDirectory(path) {
        Integration.downloads.unregister(waitFn);
        resolve(path);
        return Promise.resolve();
      },
    });
    Integration.downloads.register(waitFn);
  });
}

// Tests

/**
 * Executes a download and checks its basic properties after construction.
 * The download is started by constructing the simplest Download object with
 * the "copy" saver, or using the legacy nsITransfer interface.
 */
add_task(async function test_basic() {
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can check its basic properties before it starts.
    download = await Downloads.createDownload({
      source: { url: httpUrl("source.txt") },
      target: { path: targetFile.path },
      saver: { type: "copy" },
    });

    Assert.equal(download.source.url, httpUrl("source.txt"));
    Assert.equal(download.target.path, targetFile.path);

    await download.start();
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, thus we must check its basic properties while in progress.
    download = await promiseStartLegacyDownload(null, { targetFile });

    Assert.equal(download.source.url, httpUrl("source.txt"));
    Assert.equal(download.target.path, targetFile.path);

    await promiseDownloadStopped(download);
  }

  // Check additional properties on the finished download.
  Assert.equal(download.source.referrerInfo, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);
});

/**
 * Executes a download with the tryToKeepPartialData property set, and ensures
 * that the file is saved correctly.  When testing DownloadLegacySaver, the
 * download is executed using the nsIExternalHelperAppService component.
 */
add_task(async function test_basic_tryToKeepPartialData() {
  let download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });
  continueResponses();
  await promiseDownloadStopped(download);

  // The target file should now have been created, and the ".part" file deleted.
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  Assert.equal(32, download.saver.getSha256Hash().length);
});

/**
 * Tests that the channelIsForDownload property is set for the network request,
 * and that the request is marked as throttleable.
 */
add_task(async function test_channelIsForDownload_classFlags() {
  let downloadChannel = null;

  // We use a different method based on whether we are testing legacy downloads.
  if (!gUseLegacySaver) {
    let download = await Downloads.createDownload({
      source: {
        url: httpUrl("interruptible_resumable.txt"),
        async adjustChannel(channel) {
          downloadChannel = channel;
        },
      },
      target: getTempFile(TEST_TARGET_FILE_NAME).path,
    });
    await download.start();
  } else {
    // Start a download using nsIExternalHelperAppService, but ensure it cannot
    // finish before we retrieve the "request" property.
    mustInterruptResponses();
    let download = await promiseStartExternalHelperAppServiceDownload();
    downloadChannel = download.saver.request;
    continueResponses();
    await promiseDownloadStopped(download);
  }

  Assert.ok(
    downloadChannel.QueryInterface(Ci.nsIHttpChannelInternal)
      .channelIsForDownload
  );

  // Throttleable is the only class flag assigned to downloads.
  Assert.equal(
    downloadChannel.QueryInterface(Ci.nsIClassOfService).classFlags,
    Ci.nsIClassOfService.Throttleable
  );
});

/**
 * Tests the permissions of the final target file once the download finished.
 */
add_task(async function test_unix_permissions() {
  // This test is only executed on some Desktop systems.
  if (
    Services.appinfo.OS != "Darwin" &&
    Services.appinfo.OS != "Linux" &&
    Services.appinfo.OS != "WINNT"
  ) {
    info("Skipping test.");
    return;
  }

  let launcherPath = getTempFile("app-launcher").path;

  for (let autoDelete of [false, true]) {
    for (let isPrivate of [false, true]) {
      for (let launchWhenSucceeded of [false, true]) {
        info(
          "Checking " +
            JSON.stringify({ autoDelete, isPrivate, launchWhenSucceeded })
        );

        Services.prefs.setBoolPref(kDeleteTempFileOnExit, autoDelete);

        let download;
        if (!gUseLegacySaver) {
          download = await Downloads.createDownload({
            source: { url: httpUrl("source.txt"), isPrivate },
            target: getTempFile(TEST_TARGET_FILE_NAME).path,
            launchWhenSucceeded,
            launcherPath,
          });
          await download.start();
        } else {
          download = await promiseStartLegacyDownload(httpUrl("source.txt"), {
            isPrivate,
            launchWhenSucceeded,
            launcherPath: launchWhenSucceeded && launcherPath,
          });
          await promiseDownloadStopped(download);
        }

        let isTemporary = launchWhenSucceeded && isPrivate;
        let stat = await IOUtils.stat(download.target.path);
        if (Services.appinfo.OS == "WINNT") {
          // On Windows
          // Temporary downloads should be read-only
          Assert.equal(stat.permissions, isTemporary ? 0o444 : 0o666);
        } else {
          // On Linux, Mac
          // Temporary downloads should be read-only and not accessible to other
          // users, while permanently downloaded files should be readable and
          // writable as specified by the system umask.
          Assert.equal(
            stat.permissions,
            isTemporary ? 0o400 : 0o666 & ~Services.sysinfo.getProperty("umask")
          );
        }
      }
    }
  }

  // Clean up the changes to the preference.
  Services.prefs.clearUserPref(kDeleteTempFileOnExit);
});

/**
 * Tests the MacOS xattr com.apple.metadata:kMDItemWhereFroms information of the final target
 * once the download finished. This xattr should contain both the download source URL as well
 * as the referrer for that source, unless the referrer policy would have blocked tracking the
 * referrer.
 */
add_task(async function test_macos_xattrItemWhereFroms() {
  // This test is only executed on MacOS.
  if (Services.appinfo.OS != "Darwin") {
    info("Skipping test.");
    return;
  }

  const sourceUrl = httpUrl("source.txt");
  const xattrName = "com.apple.metadata:kMDItemWhereFroms";

  let unsafeReferrerInfo = new ReferrerInfo(
    Ci.nsIReferrerInfo.UNSAFE_URL,
    true,
    NetUtil.newURI(TEST_REFERRER_URL)
  );
  Assert.notEqual(unsafeReferrerInfo, null);

  let noReferrerInfo = new ReferrerInfo(
    Ci.nsIReferrerInfo.NO_REFERRER,
    true,
    NetUtil.newURI(TEST_REFERRER_URL)
  );

  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);

  try {
    if (!gUseLegacySaver) {
      let download = await Downloads.createDownload({
        source: { url: sourceUrl, referrerInfo: unsafeReferrerInfo },
        target: targetFile.path,
      });
      await download.start();
    } else {
      let download = await promiseStartLegacyDownload(sourceUrl, {
        targetFile,
        referrerInfo: unsafeReferrerInfo,
      });
      await promiseDownloadStopped(download);
    }
    await promiseVerifyContents(targetFile.path, TEST_DATA_SHORT);

    // Check the ItemWhereFroms xattr
    let plist = new TextDecoder().decode(
      await IOUtils.getMacXAttr(targetFile.path, xattrName)
    );
    info(plist);
    // Test that the download source is captured in the xattr
    Assert.ok(plist.includes(sourceUrl));
    // Test that the referrer is in the xattr when the Referrer-policy doesn't prohibit it
    Assert.ok(plist.includes(TEST_REFERRER_URL));
  } finally {
    await IOUtils.remove(targetFile.path);
  }

  targetFile = getTempFile(TEST_TARGET_FILE_NAME);

  info(targetFile.path);
  try {
    if (!gUseLegacySaver) {
      let download = await Downloads.createDownload({
        source: { url: sourceUrl, referrerInfo: noReferrerInfo },
        target: targetFile.path,
      });
      await download.start();
    } else {
      let download = await promiseStartLegacyDownload(sourceUrl, {
        targetFile,
        referrerInfo: noReferrerInfo,
      });
      await promiseDownloadStopped(download);
    }
    await promiseVerifyContents(targetFile.path, TEST_DATA_SHORT);
    // Check the ItemWhereFroms xattr
    let plist = new TextDecoder().decode(
      await IOUtils.getMacXAttr(targetFile.path, xattrName)
    );
    info(plist);
    // Test that the download source is captured in the xattr
    Assert.ok(plist.includes(sourceUrl));
    // Test that the referrer is not in the xattr when the Referrer-policy prohibits it
    Assert.ok(!plist.includes(TEST_REFERRER_URL));
  } finally {
    await IOUtils.remove(targetFile.path);
  }
});

/**
 * Tests the zone information of the final target once the download finished.
 */
add_task(async function test_windows_zoneInformation() {
  // This test is only executed on Windows, and in order to work correctly it
  // requires the local user applicaton data directory to be on an NTFS file
  // system. We use this directory because it is more likely to be on the local
  // system installation drive, while the temporary directory used by the test
  // environment is on the same drive as the test sources.
  if (Services.appinfo.OS != "WINNT") {
    info("Skipping test.");
    return;
  }

  let normalTargetFile = await IOUtils.getFile(
    Services.dirsvc.get("LocalAppData", Ci.nsIFile).path,
    "xpcshell-download-test.txt"
  );

  // The template file name length is more than MAX_PATH characters. The final
  // full path will be shortened to MAX_PATH length by the createUnique call.
  let longTargetFile = await IOUtils.getFile(
    Services.dirsvc.get("LocalAppData", Ci.nsIFile).path,
    "T".repeat(256) + ".txt"
  );
  longTargetFile.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o600);

  const httpSourceUrl = httpUrl("source.txt");
  const dataSourceUrl = "data:text/html," + TEST_DATA_SHORT;

  function createReferrerInfo(
    aReferrer,
    aRefererPolicy = Ci.nsIReferrerInfo.EMPTY
  ) {
    return new ReferrerInfo(aRefererPolicy, true, NetUtil.newURI(aReferrer));
  }

  const tests = [
    {
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=" + httpSourceUrl + "\r\n",
    },
    {
      targetFile: longTargetFile,
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=" + httpSourceUrl + "\r\n",
    },
    {
      sourceUrl: dataSourceUrl,
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=about:internet\r\n",
    },
    {
      options: {
        referrerInfo: createReferrerInfo(
          TEST_REFERRER_URL,
          Ci.nsIReferrerInfo.UNSAFE_URL
        ),
      },
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\n" +
        "ReferrerUrl=" +
        TEST_REFERRER_URL +
        "\r\n" +
        "HostUrl=" +
        httpSourceUrl +
        "\r\n",
    },
    {
      options: { referrerInfo: createReferrerInfo(dataSourceUrl) },
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=" + httpSourceUrl + "\r\n",
    },
    {
      options: {
        referrerInfo: createReferrerInfo("http://example.com/a\rb\nc"),
      },
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\n" +
        "ReferrerUrl=http://example.com/\r\n" +
        "HostUrl=" +
        httpSourceUrl +
        "\r\n",
    },
    {
      options: { isPrivate: true },
      expectedZoneId: "[ZoneTransfer]\r\nZoneId=3\r\n",
    },
    {
      options: {
        referrerInfo: createReferrerInfo(TEST_REFERRER_URL),
        isPrivate: true,
      },
      expectedZoneId: "[ZoneTransfer]\r\nZoneId=3\r\n",
    },
    {
      options: {
        originalUrl: "https://original.url.com",
      },
      expectedZoneId:
        "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://original.url.com/\r\n",
    },
  ];
  for (const test of tests) {
    const sourceUrl = test.sourceUrl || httpSourceUrl;
    const targetFile = test.targetFile || normalTargetFile;
    info(targetFile.path);
    try {
      if (!gUseLegacySaver) {
        let download = await Downloads.createDownload({
          source: test.options
            ? Object.assign({ url: sourceUrl }, test.options)
            : sourceUrl,
          target: targetFile.path,
        });
        await download.start();
      } else {
        let download = await promiseStartLegacyDownload(
          sourceUrl,
          Object.assign({ targetFile }, test.options || {})
        );
        await promiseDownloadStopped(download);
      }
      await promiseVerifyContents(targetFile.path, TEST_DATA_SHORT);

      let path = targetFile.path + ":Zone.Identifier";
      if (Services.appinfo.OS === "WINNT") {
        path = PathUtils.toExtendedWindowsPath(path);
      }

      Assert.equal(await IOUtils.readUTF8(path), test.expectedZoneId);
    } finally {
      await IOUtils.remove(targetFile.path);
    }
  }
});

/**
 * Checks the referrer for downloads.
 */
add_task(async function test_referrer() {
  let sourcePath = "/test_referrer.txt";
  let sourceUrl = httpUrl("test_referrer.txt");
  let dataSourceUrl = "data:text/html,<html><body></body></html>";

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  registerCleanupFunction(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);

    Assert.ok(aRequest.hasHeader("Referer"));
    Assert.equal(aRequest.getHeader("Referer"), TEST_REFERRER_URL);
  });
  let download;
  let referrerInfo = new ReferrerInfo(
    Ci.nsIReferrerInfo.UNSAFE_URL,
    true,
    NetUtil.newURI(TEST_REFERRER_URL)
  );

  if (!gUseLegacySaver) {
    let targetFile = getTempFile(TEST_TARGET_FILE_NAME);
    let targetPath = targetFile.path;

    download = await Downloads.createDownload({
      source: { url: sourceUrl, referrerInfo },
      target: targetPath,
    });

    Assert.ok(download.source.referrerInfo.equals(referrerInfo));
    await download.start();

    download = await Downloads.createDownload({
      source: { url: sourceUrl, referrerInfo, isPrivate: true },
      target: targetPath,
    });
    Assert.ok(download.source.referrerInfo.equals(referrerInfo));
    await download.start();

    // Test the download still works for non-HTTP channel with referrer.
    download = await Downloads.createDownload({
      source: { url: dataSourceUrl, referrerInfo },
      target: targetPath,
    });
    Assert.ok(download.source.referrerInfo.equals(referrerInfo));
    await download.start();
  } else {
    download = await promiseStartLegacyDownload(sourceUrl, {
      referrerInfo,
    });
    await promiseDownloadStopped(download);
    checkEqualReferrerInfos(download.source.referrerInfo, referrerInfo);

    download = await promiseStartLegacyDownload(sourceUrl, {
      referrerInfo,
      isPrivate: true,
    });
    await promiseDownloadStopped(download);
    checkEqualReferrerInfos(download.source.referrerInfo, referrerInfo);

    download = await promiseStartLegacyDownload(dataSourceUrl, {
      referrerInfo,
    });
    await promiseDownloadStopped(download);
    Assert.equal(download.source.referrerInfo, null);
  }

  cleanup();
});

/**
 * Checks the adjustChannel callback for downloads.
 */
add_task(async function test_adjustChannel() {
  const sourcePath = "/test_post.txt";
  const sourceUrl = httpUrl("test_post.txt");
  const targetPath = getTempFile(TEST_TARGET_FILE_NAME).path;
  const customHeader = { name: "X-Answer", value: "42" };
  const postData = "Don't Panic";

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  registerCleanupFunction(cleanup);

  gHttpServer.registerPathHandler(sourcePath, aRequest => {
    Assert.equal(aRequest.method, "POST");

    Assert.ok(aRequest.hasHeader(customHeader.name));
    Assert.equal(aRequest.getHeader(customHeader.name), customHeader.value);

    const stream = aRequest.bodyInputStream;
    const body = NetUtil.readInputStreamToString(stream, stream.available());
    Assert.equal(body, postData);
  });

  function adjustChannel(channel) {
    channel.QueryInterface(Ci.nsIHttpChannel);
    channel.setRequestHeader(customHeader.name, customHeader.value, false);

    const stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
      Ci.nsIStringInputStream
    );
    stream.setByteStringData(postData);

    channel.QueryInterface(Ci.nsIUploadChannel2);
    channel.explicitSetUploadStream(stream, null, -1, "POST");

    return Promise.resolve();
  }

  const download = await Downloads.createDownload({
    source: { url: sourceUrl, adjustChannel },
    target: targetPath,
  });
  Assert.equal(download.source.adjustChannel, adjustChannel);
  Assert.equal(download.toSerializable(), null);
  await download.start();

  cleanup();
});

/**
 * Checks initial and final state and progress for a successful download.
 */
add_task(async function test_initial_final_state() {
  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can check its state before it starts.
    download = await promiseNewDownload();

    Assert.ok(download.stopped);
    Assert.ok(!download.succeeded);
    Assert.ok(!download.canceled);
    Assert.strictEqual(download.error, null);
    Assert.equal(download.progress, 0);
    Assert.strictEqual(download.startTime, null);
    Assert.ok(!download.target.exists);
    Assert.equal(download.target.size, 0);

    await download.start();
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, thus we cannot check its initial state.
    download = await promiseStartLegacyDownload();
    await promiseDownloadStopped(download);
  }

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);
  Assert.equal(download.progress, 100);
  Assert.ok(isValidDate(download.startTime));
  Assert.ok(download.target.exists);
  Assert.equal(
    await expectNonZeroDownloadTargetSize(download.target),
    TEST_DATA_SHORT.length
  );
});

/**
 * Checks the notification of the final download state.
 */
add_task(async function test_final_state_notified() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));

  let onchangeNotified = false;
  let lastNotifiedStopped;
  let lastNotifiedProgress;
  download.onchange = function () {
    onchangeNotified = true;
    lastNotifiedStopped = download.stopped;
    lastNotifiedProgress = download.progress;
  };

  // Allow the download to complete.
  let promiseAttempt = download.start();
  continueResponses();
  await promiseAttempt;

  // The view should have been notified before the download completes.
  Assert.ok(onchangeNotified);
  Assert.ok(lastNotifiedStopped);
  Assert.equal(lastNotifiedProgress, 100);
});

/**
 * Checks intermediate progress for a successful download.
 */
add_task(async function test_intermediate_progress() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));

  await promiseDownloadMidway(download);

  Assert.ok(download.hasProgress);
  Assert.equal(download.currentBytes, TEST_DATA_SHORT.length);
  Assert.equal(download.totalBytes, TEST_DATA_SHORT.length * 2);

  // The final file size should not be computed for in-progress downloads.
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);

  // Continue after the first chunk of data is fully received.
  continueResponses();
  await promiseDownloadStopped(download);

  Assert.ok(download.stopped);
  Assert.equal(download.progress, 100);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Downloads a file with a "Content-Length" of 0 and checks the progress.
 */
add_task(async function test_empty_progress() {
  let download = await promiseStartDownload(httpUrl("empty.txt"));
  await promiseDownloadStopped(download);

  Assert.ok(download.stopped);
  Assert.ok(download.hasProgress);
  Assert.equal(download.progress, 100);
  Assert.equal(download.currentBytes, 0);
  Assert.equal(download.totalBytes, 0);

  // We should have received the content type even for an empty file.
  Assert.equal(download.contentType, "text/plain");

  Assert.equal((await IOUtils.stat(download.target.path)).size, 0);
  Assert.ok(download.target.exists);
  Assert.equal(download.target.size, 0);
});

/**
 * Downloads a file with a "Content-Length" of 0 with the tryToKeepPartialData
 * property set, and ensures that the file is saved correctly.
 */
add_task(async function test_empty_progress_tryToKeepPartialData() {
  // Start a new download and configure it to keep partially downloaded data.
  let download;
  if (!gUseLegacySaver) {
    let targetFilePath = getTempFile(TEST_TARGET_FILE_NAME).path;
    download = await Downloads.createDownload({
      source: httpUrl("empty.txt"),
      target: { path: targetFilePath, partFilePath: targetFilePath + ".part" },
    });
    download.tryToKeepPartialData = true;
    download.start().catch(() => {});
  } else {
    // Start a download using nsIExternalHelperAppService, that is configured
    // to keep partially downloaded data by default.
    download = await promiseStartExternalHelperAppServiceDownload(
      httpUrl("empty.txt")
    );
  }
  await promiseDownloadStopped(download);

  // The target file should now have been created, and the ".part" file deleted.
  Assert.equal((await IOUtils.stat(download.target.path)).size, 0);
  Assert.ok(download.target.exists);
  Assert.equal(download.target.size, 0);

  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  Assert.equal(32, download.saver.getSha256Hash().length);
});

/**
 * Downloads an empty file with no "Content-Length" and checks the progress.
 */
add_task(async function test_empty_noprogress() {
  let sourcePath = "/test_empty_noprogress.txt";
  let sourceUrl = httpUrl("test_empty_noprogress.txt");
  let deferRequestReceived = Promise.withResolvers();

  // Register an interruptible handler that notifies us when the request occurs.
  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  registerCleanupFunction(cleanup);

  registerInterruptibleHandler(
    sourcePath,
    function firstPart(aRequest, aResponse) {
      aResponse.setHeader("Content-Type", "text/plain", false);
      deferRequestReceived.resolve();
    },
    function secondPart() {}
  );

  // Start the download, without allowing the request to finish.
  mustInterruptResponses();
  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can hook its onchange callback that will be notified when the
    // download starts.
    download = await promiseNewDownload(sourceUrl);

    download.onchange = function () {
      if (!download.stopped) {
        Assert.ok(!download.hasProgress);
        Assert.equal(download.currentBytes, 0);
        Assert.equal(download.totalBytes, 0);
      }
    };

    download.start().catch(() => {});
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, and it may have already made all needed property change
    // notifications, thus there is no point in checking the onchange callback.
    download = await promiseStartLegacyDownload(sourceUrl);
  }

  // Wait for the request to be received by the HTTP server, but don't allow the
  // request to finish yet.  Before checking the download state, wait for the
  // events to be processed by the client.
  await deferRequestReceived.promise;
  await promiseExecuteSoon();

  // Check that this download has no progress report.
  Assert.ok(!download.stopped);
  Assert.ok(!download.hasProgress);
  Assert.equal(download.currentBytes, 0);
  Assert.equal(download.totalBytes, 0);

  // Now allow the response to finish.
  continueResponses();
  await promiseDownloadStopped(download);

  // We should have received the content type even if no progress is reported.
  Assert.equal(download.contentType, "text/plain");

  // Verify the state of the completed download.
  Assert.ok(download.stopped);
  Assert.ok(!download.hasProgress);
  Assert.equal(download.progress, 100);
  Assert.equal(download.currentBytes, 0);
  Assert.equal(download.totalBytes, 0);
  Assert.ok(download.target.exists);
  Assert.equal(download.target.size, 0);

  Assert.equal((await IOUtils.stat(download.target.path)).size, 0);
});

/**
 * Calls the "start" method two times before the download is finished.
 */
add_task(async function test_start_twice() {
  mustInterruptResponses();

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can start the download later during the test.
    download = await promiseNewDownload(httpUrl("interruptible.txt"));
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created.  Effectively, we are starting the download three times.
    download = await promiseStartLegacyDownload(httpUrl("interruptible.txt"));
  }

  // Call the start method two times.
  let promiseAttempt1 = download.start();
  let promiseAttempt2 = download.start();

  // Allow the download to finish.
  continueResponses();

  // Both promises should now be resolved.
  await promiseAttempt1;
  await promiseAttempt2;

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download and verifies that its state is reported correctly.
 */
add_task(async function test_cancel_midway() {
  mustInterruptResponses();

  // In this test case, we execute different checks that are only possible with
  // DownloadCopySaver or DownloadLegacySaver respectively.
  let download;
  let options = {};
  if (!gUseLegacySaver) {
    download = await promiseNewDownload(httpUrl("interruptible.txt"));
  } else {
    download = await promiseStartLegacyDownload(
      httpUrl("interruptible.txt"),
      options
    );
  }

  // Cancel the download after receiving the first part of the response.
  let deferCancel = Promise.withResolvers();
  let onchange = function () {
    if (!download.stopped && !download.canceled && download.progress == 50) {
      // Cancel the download immediately during the notification.
      deferCancel.resolve(download.cancel());

      // The state change happens immediately after calling "cancel", but
      // temporary files or part files may still exist at this point.
      Assert.ok(download.canceled);
    }
  };

  // Register for the notification, but also call the function directly in
  // case the download already reached the expected progress.  This may happen
  // when using DownloadLegacySaver.
  download.onchange = onchange;
  onchange();

  let promiseAttempt;
  if (!gUseLegacySaver) {
    promiseAttempt = download.start();
  }

  // Wait on the promise returned by the "cancel" method to ensure that the
  // cancellation process finished and temporary files were removed.
  await deferCancel.promise;

  if (gUseLegacySaver) {
    // The nsIWebBrowserPersist instance should have been canceled now.
    Assert.equal(options.outPersist.result, Cr.NS_ERROR_ABORT);
  }

  Assert.ok(download.stopped);
  Assert.ok(download.canceled);
  Assert.strictEqual(download.error, null);
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);

  Assert.equal(false, await IOUtils.exists(download.target.path));

  // Progress properties are not reset by canceling.
  Assert.equal(download.progress, 50);
  Assert.equal(download.totalBytes, TEST_DATA_SHORT.length * 2);
  Assert.equal(download.currentBytes, TEST_DATA_SHORT.length);

  if (!gUseLegacySaver) {
    // The promise returned by "start" should have been rejected meanwhile.
    try {
      await promiseAttempt;
      do_throw("The download should have been canceled.");
    } catch (ex) {
      if (!(ex instanceof Downloads.Error)) {
        throw ex;
      }
      Assert.ok(!ex.becauseSourceFailed);
      Assert.ok(!ex.becauseTargetFailed);
    }
  }
});

/**
 * Cancels a download while keeping partially downloaded data, and verifies that
 * both the target file and the ".part" file are deleted.
 */
add_task(async function test_cancel_midway_tryToKeepPartialData() {
  let download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });

  Assert.ok(await IOUtils.exists(download.target.path));
  Assert.ok(await IOUtils.exists(download.target.partFilePath));

  await download.cancel();
  await download.removePartialData();

  Assert.ok(download.stopped);
  Assert.ok(download.canceled);
  Assert.strictEqual(download.error, null);

  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
});

/**
 * Cancels a download right after starting it.
 */
add_task(async function test_cancel_immediately() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));

  let promiseAttempt = download.start();
  Assert.ok(!download.stopped);

  let promiseCancel = download.cancel();
  Assert.ok(download.canceled);

  // At this point, we don't know whether the download has already stopped or
  // is still waiting for cancellation.  We can wait on the promise returned
  // by the "start" method to know for sure.
  try {
    await promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error)) {
      throw ex;
    }
    Assert.ok(!ex.becauseSourceFailed);
    Assert.ok(!ex.becauseTargetFailed);
  }

  Assert.ok(download.stopped);
  Assert.ok(download.canceled);
  Assert.strictEqual(download.error, null);

  Assert.equal(false, await IOUtils.exists(download.target.path));

  // Check that the promise returned by the "cancel" method has been resolved.
  await promiseCancel;
});

/**
 * Cancels and restarts a download sequentially.
 */
add_task(async function test_cancel_midway_restart() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));

  // The first time, cancel the download midway.
  await promiseDownloadMidway(download);
  await download.cancel();

  Assert.ok(download.stopped);

  // The second time, we'll provide the entire interruptible response.
  continueResponses();
  download.onchange = null;
  let promiseAttempt = download.start();

  // Download state should have already been reset.
  Assert.ok(!download.stopped);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  Assert.equal(download.progress, 0);
  Assert.equal(download.totalBytes, 0);
  Assert.equal(download.currentBytes, 0);

  await promiseAttempt;

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download and restarts it from where it stopped.
 */
add_task(async function test_cancel_midway_restart_tryToKeepPartialData() {
  let download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });
  await download.cancel();

  Assert.ok(download.stopped);
  Assert.ok(download.hasPartialData);

  // We should have kept the partial data and an empty target file placeholder.
  Assert.ok(await IOUtils.exists(download.target.path));
  await promiseVerifyContents(download.target.partFilePath, TEST_DATA_SHORT);
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);

  // Verify that the server sent the response from the start.
  Assert.equal(gMostRecentFirstBytePos, 0);

  // The second time, we'll request and obtain the second part of the response,
  // but we still stop when half of the remaining progress is reached.
  let deferMidway = Promise.withResolvers();
  download.onchange = function () {
    if (
      !download.stopped &&
      !download.canceled &&
      download.currentBytes == Math.floor((TEST_DATA_SHORT.length * 3) / 2)
    ) {
      download.onchange = null;
      deferMidway.resolve();
    }
  };

  mustInterruptResponses();
  let promiseAttempt = download.start();

  // Continue when the number of bytes we received is correct, then check that
  // progress is at about 75 percent.  The exact figure may vary because of
  // rounding issues, since the total number of bytes in the response might not
  // be a multiple of four.
  await deferMidway.promise;
  Assert.ok(download.progress > 72 && download.progress < 78);

  // Now we allow the download to finish.
  continueResponses();
  await promiseAttempt;

  // Check that the server now sent the second part only.
  Assert.equal(gMostRecentFirstBytePos, TEST_DATA_SHORT.length);

  // The target file should now have been created, and the ".part" file deleted.
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
});

/**
 * Cancels a download while keeping partially downloaded data, then removes the
 * data and restarts the download from the beginning.
 */
add_task(async function test_cancel_midway_restart_removePartialData() {
  let download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });
  await download.cancel();
  await download.removePartialData();

  Assert.ok(!download.hasPartialData);
  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);

  // The second time, we'll request and obtain the entire response again.
  continueResponses();
  await download.start();

  // Verify that the server sent the response from the start.
  Assert.equal(gMostRecentFirstBytePos, 0);

  // The target file should now have been created, and the ".part" file deleted.
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
});

/**
 * Cancels a download while keeping partially downloaded data, then removes the
 * data and restarts the download from the beginning without keeping the partial
 * data anymore.
 */
add_task(
  async function test_cancel_midway_restart_tryToKeepPartialData_false() {
    let download = await promiseStartDownload_tryToKeepPartialData({
      useLegacySaver: gUseLegacySaver,
    });
    await download.cancel();

    download.tryToKeepPartialData = false;

    // The above property change does not affect existing partial data.
    Assert.ok(download.hasPartialData);
    await promiseVerifyContents(download.target.partFilePath, TEST_DATA_SHORT);

    await download.removePartialData();
    Assert.equal(false, await IOUtils.exists(download.target.path));
    Assert.equal(false, await IOUtils.exists(download.target.partFilePath));

    // Restart the download from the beginning.
    mustInterruptResponses();
    download.start().catch(() => {});

    await promiseDownloadMidway(download);
    await promisePartFileReady(download);

    // While the download is in progress, we should still have a ".part" file.
    Assert.ok(!download.hasPartialData);
    Assert.ok(await IOUtils.exists(download.target.path));
    Assert.ok(await IOUtils.exists(download.target.partFilePath));

    // On Unix, verify that the file with the partially downloaded data is not
    // accessible by other users on the system.
    if (Services.appinfo.OS == "Darwin" || Services.appinfo.OS == "Linux") {
      Assert.equal(
        (await IOUtils.stat(download.target.partFilePath)).permissions,
        0o600
      );
    }

    await download.cancel();

    // The ".part" file should be deleted now that the download is canceled.
    Assert.ok(!download.hasPartialData);
    Assert.equal(false, await IOUtils.exists(download.target.path));
    Assert.equal(false, await IOUtils.exists(download.target.partFilePath));

    // The third time, we'll request and obtain the entire response again.
    continueResponses();
    await download.start();

    // Verify that the server sent the response from the start.
    Assert.equal(gMostRecentFirstBytePos, 0);

    // The target file should now have been created, and the ".part" file deleted.
    await promiseVerifyTarget(
      download.target,
      TEST_DATA_SHORT + TEST_DATA_SHORT
    );
    Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  }
);

/**
 * Cancels a download right after starting it, then restarts it immediately.
 */
add_task(async function test_cancel_immediately_restart_immediately() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));
  let promiseAttempt = download.start();

  Assert.ok(!download.stopped);

  download.cancel();
  Assert.ok(download.canceled);

  let promiseRestarted = download.start();
  Assert.ok(!download.stopped);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  Assert.equal(download.hasProgress, false);
  Assert.equal(download.progress, 0);
  Assert.equal(download.totalBytes, 0);
  Assert.equal(download.currentBytes, 0);

  // Ensure the next request is now allowed to complete, regardless of whether
  // the canceled request was received by the server or not.
  continueResponses();
  try {
    await promiseAttempt;
    // If we get here, it means that the first attempt actually succeeded.  In
    // fact, this could be a valid outcome, because the cancellation request may
    // not have been processed in time before the download finished.
    info("The download should have been canceled.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error)) {
      throw ex;
    }
    Assert.ok(!ex.becauseSourceFailed);
    Assert.ok(!ex.becauseTargetFailed);
  }

  await promiseRestarted;

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download midway, then restarts it immediately.
 */
add_task(async function test_cancel_midway_restart_immediately() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));
  let promiseAttempt = download.start();

  // The first time, cancel the download midway.
  await promiseDownloadMidway(download);
  download.cancel();
  Assert.ok(download.canceled);

  let promiseRestarted = download.start();
  Assert.ok(!download.stopped);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  Assert.equal(download.hasProgress, false);
  Assert.equal(download.progress, 0);
  Assert.equal(download.totalBytes, 0);
  Assert.equal(download.currentBytes, 0);

  // The second request is allowed to complete.
  continueResponses();
  try {
    await promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error)) {
      throw ex;
    }
    Assert.ok(!ex.becauseSourceFailed);
    Assert.ok(!ex.becauseTargetFailed);
  }

  await promiseRestarted;

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Calls the "cancel" method on a successful download.
 */
add_task(async function test_cancel_successful() {
  let download = await promiseStartDownload();
  await promiseDownloadStopped(download);

  // The cancel method should succeed with no effect.
  await download.cancel();

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);
});

/**
 * Calls the "cancel" method two times in a row.
 */
add_task(async function test_cancel_twice() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));

  let promiseAttempt = download.start();
  Assert.ok(!download.stopped);

  let promiseCancel1 = download.cancel();
  Assert.ok(download.canceled);
  let promiseCancel2 = download.cancel();

  try {
    await promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error)) {
      throw ex;
    }
    Assert.ok(!ex.becauseSourceFailed);
    Assert.ok(!ex.becauseTargetFailed);
  }

  // Both promises should now be resolved.
  await promiseCancel1;
  await promiseCancel2;

  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);
  Assert.ok(download.canceled);
  Assert.strictEqual(download.error, null);

  Assert.equal(false, await IOUtils.exists(download.target.path));
});

/**
 * Checks the "refresh" method for succeeded downloads.
 */
add_task(async function test_refresh_succeeded() {
  let download = await promiseStartDownload();
  await promiseDownloadStopped(download);

  // The DownloadTarget properties should be the same after calling "refresh".
  await download.refresh();
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);

  // If the file is removed, only the "exists" property should change, and the
  // "size" property should keep its previous value.
  await IOUtils.move(download.target.path, `${download.target.path}.old`);
  await download.refresh();
  Assert.ok(!download.target.exists);
  Assert.equal(
    await expectNonZeroDownloadTargetSize(download.target),
    TEST_DATA_SHORT.length
  );

  // The DownloadTarget properties should be restored when the file is put back.
  await IOUtils.move(`${download.target.path}.old`, download.target.path);
  await download.refresh();
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);
});

/**
 * Checks that a download cannot be restarted after the "finalize" method.
 */
add_task(async function test_finalize() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible.txt"));

  let promiseFinalized = download.finalize();

  try {
    await download.start();
    do_throw("It should not be possible to restart after finalization.");
  } catch (ex) {}

  await promiseFinalized;

  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);
  Assert.ok(download.canceled);
  Assert.strictEqual(download.error, null);

  Assert.equal(false, await IOUtils.exists(download.target.path));
});

/**
 * Checks that the "finalize" method can remove partially downloaded data.
 */
add_task(async function test_finalize_tryToKeepPartialData() {
  // Check finalization without removing partial data.
  let download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });
  await download.finalize();

  Assert.ok(download.hasPartialData);
  Assert.ok(await IOUtils.exists(download.target.path));
  Assert.ok(await IOUtils.exists(download.target.partFilePath));

  // Clean up.
  await download.removePartialData();

  // Check finalization while removing partial data.
  download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });
  await download.finalize(true);

  Assert.ok(!download.hasPartialData);
  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
});

/**
 * Checks that whenSucceeded returns a promise that is resolved after a restart.
 */
add_task(async function test_whenSucceeded_after_restart() {
  mustInterruptResponses();

  let promiseSucceeded;

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can verify getting a reference before the first download attempt.
    download = await promiseNewDownload(httpUrl("interruptible.txt"));
    promiseSucceeded = download.whenSucceeded();
    download.start().catch(() => {});
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, thus we cannot get the reference before the first attempt.
    download = await promiseStartLegacyDownload(httpUrl("interruptible.txt"));
    promiseSucceeded = download.whenSucceeded();
  }

  // Cancel the first download attempt.
  await download.cancel();

  // The second request is allowed to complete.
  continueResponses();
  download.start().catch(() => {});

  // Wait for the download to finish by waiting on the whenSucceeded promise.
  await promiseSucceeded;

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Ensures download error details are reported on network failures.
 */
add_task(async function test_error_source() {
  let serverSocket = startFakeServer();
  try {
    let sourceUrl = "http://localhost:" + serverSocket.port + "/source.txt";

    let download;
    try {
      if (!gUseLegacySaver) {
        // When testing DownloadCopySaver, we want to check that the promise
        // returned by the "start" method is rejected.
        download = await promiseNewDownload(sourceUrl);

        Assert.strictEqual(download.error, null);

        await download.start();
      } else {
        // When testing DownloadLegacySaver, we cannot be sure whether we are
        // testing the promise returned by the "start" method or we are testing
        // the "error" property checked by promiseDownloadStopped.  This happens
        // because we don't have control over when the download is started.
        download = await promiseStartLegacyDownload(sourceUrl);
        await promiseDownloadStopped(download);
      }
      do_throw("The download should have failed.");
    } catch (ex) {
      if (!(ex instanceof Downloads.Error) || !ex.becauseSourceFailed) {
        throw ex;
      }
      // A specific error object is thrown when reading from the source fails.
    }

    // Check the properties now that the download stopped.
    Assert.ok(download.stopped);
    Assert.ok(!download.canceled);
    Assert.notStrictEqual(download.error, null);
    Assert.ok(download.error.becauseSourceFailed);
    Assert.ok(!download.error.becauseTargetFailed);

    Assert.equal(false, await IOUtils.exists(download.target.path));
    Assert.ok(!download.target.exists);
    Assert.equal(download.target.size, 0);
  } finally {
    serverSocket.close();
  }
});

/**
 * Ensures a download error is reported when receiving less bytes than what was
 * specified in the Content-Length header.
 */
add_task(async function test_error_source_partial() {
  let sourceUrl = httpUrl("shorter-than-content-length-http-1-1.txt");

  let enforcePref = Services.prefs.getBoolPref(
    "network.http.enforce-framing.http1"
  );
  Services.prefs.setBoolPref("network.http.enforce-framing.http1", true);

  function cleanup() {
    Services.prefs.setBoolPref(
      "network.http.enforce-framing.http1",
      enforcePref
    );
  }
  registerCleanupFunction(cleanup);

  let download;
  try {
    if (!gUseLegacySaver) {
      // When testing DownloadCopySaver, we want to check that the promise
      // returned by the "start" method is rejected.
      download = await promiseNewDownload(sourceUrl);

      Assert.strictEqual(download.error, null);

      await download.start();
    } else {
      // When testing DownloadLegacySaver, we cannot be sure whether we are
      // testing the promise returned by the "start" method or we are testing
      // the "error" property checked by promiseDownloadStopped.  This happens
      // because we don't have control over when the download is started.
      download = await promiseStartLegacyDownload(sourceUrl);
      await promiseDownloadStopped(download);
    }
    do_throw("The download should have failed.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error) || !ex.becauseSourceFailed) {
      throw ex;
    }
    // A specific error object is thrown when reading from the source fails.
  }

  // Check the properties now that the download stopped.
  Assert.ok(download.stopped);
  Assert.ok(!download.canceled);
  Assert.notStrictEqual(download.error, null);
  Assert.ok(download.error.becauseSourceFailed);
  Assert.ok(!download.error.becauseTargetFailed);
  Assert.equal(download.error.result, Cr.NS_ERROR_NET_PARTIAL_TRANSFER);

  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);
});

/**
 * Ensures a download error is reported when an RST packet is received.
 */
add_task(async function test_error_source_netreset() {
  if (AppConstants.platform == "win") {
    return;
  }

  let download;
  try {
    if (!gUseLegacySaver) {
      // When testing DownloadCopySaver, we want to check that the promise
      // returned by the "start" method is rejected.
      download = await promiseNewDownload(httpUrl("netreset.txt"));

      Assert.strictEqual(download.error, null);

      await download.start();
    } else {
      // When testing DownloadLegacySaver, we cannot be sure whether we are
      // testing the promise returned by the "start" method or we are testing
      // the "error" property checked by promiseDownloadStopped.  This happens
      // because we don't have control over when the download is started.
      download = await promiseStartLegacyDownload(httpUrl("netreset.txt"));
      await promiseDownloadStopped(download);
    }
    do_throw("The download should have failed.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error) || !ex.becauseSourceFailed) {
      throw ex;
    }
    // A specific error object is thrown when reading from the source fails.
  }

  // Check the properties now that the download stopped.
  Assert.ok(download.stopped);
  Assert.ok(!download.canceled);
  Assert.notStrictEqual(download.error, null);
  Assert.ok(download.error.becauseSourceFailed);
  Assert.ok(!download.error.becauseTargetFailed);
  Assert.equal(download.error.result, Cr.NS_ERROR_NET_RESET);

  Assert.equal(false, await IOUtils.exists(download.target.path));
});

/**
 * Ensures download error details are reported on local writing failures.
 */
add_task(async function test_error_target() {
  // Create a file without write access permissions before downloading.
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);
  targetFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0);
  try {
    let download;
    try {
      if (!gUseLegacySaver) {
        // When testing DownloadCopySaver, we want to check that the promise
        // returned by the "start" method is rejected.
        download = await Downloads.createDownload({
          source: httpUrl("source.txt"),
          target: targetFile,
        });
        await download.start();
      } else {
        // When testing DownloadLegacySaver, we cannot be sure whether we are
        // testing the promise returned by the "start" method or we are testing
        // the "error" property checked by promiseDownloadStopped.  This happens
        // because we don't have control over when the download is started.
        download = await promiseStartLegacyDownload(null, { targetFile });
        await promiseDownloadStopped(download);
      }
      do_throw("The download should have failed.");
    } catch (ex) {
      if (!(ex instanceof Downloads.Error) || !ex.becauseTargetFailed) {
        throw ex;
      }
      // A specific error object is thrown when writing to the target fails.
    }

    // Check the properties now that the download stopped.
    Assert.ok(download.stopped);
    Assert.ok(!download.canceled);
    Assert.notStrictEqual(download.error, null);
    Assert.ok(download.error.becauseTargetFailed);
    Assert.ok(!download.error.becauseSourceFailed);

    // Check unserializing a download with an errorObj and restarting it will
    // clear the errorObj initially.
    let serializable = download.toSerializable();
    Assert.ok(serializable.errorObj, "Ensure we have an errorObj initially");
    let reserialized = JSON.parse(JSON.stringify(serializable));
    download = await Downloads.createDownload(reserialized);
    let promise = download.start().catch(() => {});
    serializable = download.toSerializable();
    Assert.ok(!serializable.errorObj, "Ensure we didn't persist the errorObj");
    await promise;
  } finally {
    // Restore the default permissions to allow deleting the file on Windows.
    if (targetFile.exists()) {
      targetFile.permissions = FileUtils.PERMS_FILE;
      targetFile.remove(false);
    }
  }
});

/**
 * Restarts a failed download.
 */
add_task(async function test_error_restart() {
  let download;

  // Create a file without write access permissions before downloading.
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);
  targetFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0);
  try {
    // Use DownloadCopySaver or DownloadLegacySaver based on the test run,
    // specifying the target file we created.
    if (!gUseLegacySaver) {
      download = await Downloads.createDownload({
        source: httpUrl("source.txt"),
        target: targetFile,
      });
      download.start().catch(() => {});
    } else {
      download = await promiseStartLegacyDownload(null, { targetFile });
    }
    await promiseDownloadStopped(download);
    do_throw("The download should have failed.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error) || !ex.becauseTargetFailed) {
      throw ex;
    }
    // A specific error object is thrown when writing to the target fails.
  } finally {
    // Restore the default permissions to allow deleting the file on Windows.
    if (targetFile.exists()) {
      targetFile.permissions = FileUtils.PERMS_FILE;

      // Also for Windows, rename the file before deleting.  This makes the
      // current file name available immediately for a new file, while deleting
      // in place prevents creation of a file with the same name for some time.
      targetFile.moveTo(null, targetFile.leafName + ".delete.tmp");
      targetFile.remove(false);
    }
  }

  // Restart the download and wait for completion.
  await download.start();

  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.canceled);
  Assert.strictEqual(download.error, null);
  Assert.equal(download.progress, 100);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);
});

/**
 * Executes download in both public and private modes.
 */
add_task(async function test_public_and_private() {
  let sourcePath = "/test_public_and_private.txt";
  let sourceUrl = httpUrl("test_public_and_private.txt");
  let testCount = 0;

  // Apply pref to allow all cookies.
  Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);

  function cleanup() {
    Services.prefs.clearUserPref("network.cookie.cookieBehavior");
    Services.cookies.removeAll();
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  registerCleanupFunction(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);

    if (testCount == 0) {
      // No cookies should exist for first public download.
      Assert.ok(!aRequest.hasHeader("Cookie"));
      aResponse.setHeader("Set-Cookie", "foobar=1", false);
      testCount++;
    } else if (testCount == 1) {
      // The cookie should exists for second public download.
      Assert.ok(aRequest.hasHeader("Cookie"));
      Assert.equal(aRequest.getHeader("Cookie"), "foobar=1");
      testCount++;
    } else if (testCount == 2) {
      // No cookies should exist for first private download.
      Assert.ok(!aRequest.hasHeader("Cookie"));
    }
  });

  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);
  await Downloads.fetch(sourceUrl, targetFile);
  await Downloads.fetch(sourceUrl, targetFile);

  if (!gUseLegacySaver) {
    let download = await Downloads.createDownload({
      source: { url: sourceUrl, isPrivate: true },
      target: targetFile,
    });
    await download.start();
  } else {
    let download = await promiseStartLegacyDownload(sourceUrl, {
      isPrivate: true,
    });
    await promiseDownloadStopped(download);
  }

  cleanup();
});

/**
 * Checks the startTime gets updated even after a restart.
 */
add_task(async function test_cancel_immediately_restart_and_check_startTime() {
  let download = await promiseStartDownload();

  let startTime = download.startTime;
  Assert.ok(isValidDate(download.startTime));

  await download.cancel();
  Assert.equal(download.startTime.getTime(), startTime.getTime());

  // Wait for a timeout.
  await promiseTimeout(10);

  await download.start();
  Assert.greater(download.startTime.getTime(), startTime.getTime());
});

/**
 * Executes download with content-encoding.
 */
add_task(async function test_with_content_encoding() {
  let sourcePath = "/test_with_content_encoding.txt";
  let sourceUrl = httpUrl("test_with_content_encoding.txt");

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  registerCleanupFunction(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);
    aResponse.setHeader("Content-Encoding", "gzip", false);
    aResponse.setHeader(
      "Content-Length",
      "" + TEST_DATA_SHORT_GZIP_ENCODED.length,
      false
    );

    let bos = new BinaryOutputStream(aResponse.bodyOutputStream);
    bos.writeByteArray(TEST_DATA_SHORT_GZIP_ENCODED);
  });

  let download = await promiseStartDownload(sourceUrl);
  await promiseDownloadStopped(download);

  Assert.equal(download.progress, 100);
  Assert.equal(download.totalBytes, TEST_DATA_SHORT_GZIP_ENCODED.length);

  // Ensure the content matches the decoded test data.
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);

  cleanup();
});

/**
 * Checks that the Content-Encoding is decoded even when the file extension
 * matches the encoding (bug 610679, bug 1470011). This is the default
 * behavior; the legacy behavior is checked in the test below.
 */
add_task(async function test_with_content_encoding_matching_extension() {
  Services.prefs.setBoolPref(kDecodeContentForKnownCompressedExtensions, true);

  let sourcePath = "/test_with_content_encoding_matching_extension.gz";
  let sourceUrl = httpUrl("test_with_content_encoding_matching_extension.gz");

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
    Services.prefs.clearUserPref(kDecodeContentForKnownCompressedExtensions);
  }
  registerCleanupFunction(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);
    aResponse.setHeader("Content-Encoding", "gzip", false);
    aResponse.setHeader(
      "Content-Length",
      "" + TEST_DATA_SHORT_GZIP_ENCODED.length,
      false
    );

    let bos = new BinaryOutputStream(aResponse.bodyOutputStream);
    bos.writeByteArray(TEST_DATA_SHORT_GZIP_ENCODED);
  });

  let download = await promiseStartDownload(sourceUrl);
  await promiseDownloadStopped(download);

  Assert.equal(download.progress, 100);
  Assert.equal(download.totalBytes, TEST_DATA_SHORT_GZIP_ENCODED.length);

  // Ensure the content matches the decoded test data.
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);

  cleanup();
});

/**
 * Checks that with the legacy pref disabled, the file is not decoded if the
 * extension matches the encoding.
 */
add_task(async function test_with_content_encoding_ignore_extension() {
  Services.prefs.setBoolPref(kDecodeContentForKnownCompressedExtensions, false);

  let sourcePath = "/test_with_content_encoding_ignore_extension.gz";
  let sourceUrl = httpUrl("test_with_content_encoding_ignore_extension.gz");

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
    Services.prefs.clearUserPref(kDecodeContentForKnownCompressedExtensions);
  }
  registerCleanupFunction(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);
    aResponse.setHeader("Content-Encoding", "gzip", false);
    aResponse.setHeader(
      "Content-Length",
      "" + TEST_DATA_SHORT_GZIP_ENCODED.length,
      false
    );

    let bos = new BinaryOutputStream(aResponse.bodyOutputStream);
    bos.writeByteArray(TEST_DATA_SHORT_GZIP_ENCODED);
  });

  let download = await promiseStartDownload(sourceUrl);
  await promiseDownloadStopped(download);

  Assert.equal(download.progress, 100);
  Assert.equal(download.totalBytes, TEST_DATA_SHORT_GZIP_ENCODED.length);
  Assert.equal(
    await expectNonZeroDownloadTargetSize(download.target),
    TEST_DATA_SHORT_GZIP_ENCODED.length
  );

  // Ensure the content matches the encoded test data.  We convert the data to a
  // string before executing the content check.
  await promiseVerifyTarget(
    download.target,
    String.fromCharCode.apply(String, TEST_DATA_SHORT_GZIP_ENCODED)
  );

  cleanup();
});

/**
 * Cancels and restarts a download sequentially with content-encoding.
 */
add_task(async function test_cancel_midway_restart_with_content_encoding() {
  mustInterruptResponses();

  let download = await promiseStartDownload(httpUrl("interruptible_gzip.txt"));

  // The first time, cancel the download midway.
  await new Promise(resolve => {
    let onchange = function () {
      if (
        !download.stopped &&
        !download.canceled &&
        download.currentBytes == TEST_DATA_SHORT_GZIP_ENCODED_FIRST.length
      ) {
        resolve(download.cancel());
      }
    };

    // Register for the notification, but also call the function directly in
    // case the download already reached the expected progress.
    download.onchange = onchange;
    onchange();
  });

  Assert.ok(download.stopped);

  // The second time, we'll provide the entire interruptible response.
  continueResponses();
  download.onchange = null;
  await download.start();

  Assert.equal(download.progress, 100);
  Assert.equal(download.totalBytes, TEST_DATA_SHORT_GZIP_ENCODED.length);

  await promiseVerifyTarget(download.target, TEST_DATA_SHORT);
});

/**
 * Download with parental controls enabled.
 */
add_task(async function test_blocked_parental_controls() {
  let blockFn = () => ({
    shouldBlockForParentalControls: () => Promise.resolve(true),
  });

  Integration.downloads.register(blockFn);
  function cleanup() {
    Integration.downloads.unregister(blockFn);
  }
  registerCleanupFunction(cleanup);

  let download;
  try {
    if (!gUseLegacySaver) {
      // When testing DownloadCopySaver, we want to check that the promise
      // returned by the "start" method is rejected.
      download = await promiseNewDownload();
      await download.start();
    } else {
      // When testing DownloadLegacySaver, we cannot be sure whether we are
      // testing the promise returned by the "start" method or we are testing
      // the "error" property checked by promiseDownloadStopped.  This happens
      // because we don't have control over when the download is started.
      download = await promiseStartLegacyDownload();
      await promiseDownloadStopped(download);
    }
    do_throw("The download should have blocked.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error) || !ex.becauseBlocked) {
      throw ex;
    }
    Assert.ok(ex.becauseBlockedByParentalControls);
    Assert.ok(download.error.becauseBlockedByParentalControls);
  }

  // Now that the download stopped, the target file should not exist.
  Assert.equal(false, await IOUtils.exists(download.target.path));

  cleanup();
});

/**
 * Test a download that will be blocked by Windows parental controls by
 * resulting in an HTTP status code of 450.
 */
add_task(async function test_blocked_parental_controls_httpstatus450() {
  let download;
  try {
    if (!gUseLegacySaver) {
      download = await promiseNewDownload(httpUrl("parentalblocked.zip"));
      await download.start();
    } else {
      download = await promiseStartLegacyDownload(
        httpUrl("parentalblocked.zip")
      );
      await promiseDownloadStopped(download);
    }
    do_throw("The download should have blocked.");
  } catch (ex) {
    if (!(ex instanceof Downloads.Error) || !ex.becauseBlocked) {
      throw ex;
    }
    Assert.ok(ex.becauseBlockedByParentalControls);
    Assert.ok(download.error.becauseBlockedByParentalControls);
    Assert.ok(download.stopped);
  }

  Assert.equal(false, await IOUtils.exists(download.target.path));
});

/**
 * Check that DownloadCopySaver can always retrieve the hash.
 * DownloadLegacySaver can only retrieve the hash when
 * nsIExternalHelperAppService is invoked.
 */
add_task(async function test_getSha256Hash() {
  if (!gUseLegacySaver) {
    let download = await promiseStartDownload(httpUrl("source.txt"));
    await promiseDownloadStopped(download);
    Assert.ok(download.stopped);
    Assert.equal(32, download.saver.getSha256Hash().length);
  }
});

/**
 * The `toolkit/components/downloads` tests use a custom partial override of the
 * directory service via `Integration.downloads`, defined in `./head.js`. The
 * `nsIExternalHelperAppService` has no way of knowing about that, though, so
 * we'll need to disable the override for the comparison test.
 *
 * This wrapper does that -- and more importantly, ensures that the override is
 * reenabled afterwards, regardless of how the test exited.
 *
 * (Ideally we'd do this inside the test by registering a test-specific cleanup
 * function, as with SimpleTest's `registerCurrentTaskCleanupFunction() -- but,
 * at time of writing, the xpcshell test setup doesn't support those.)
 *
 * @param {() => any} innerTask - The task to be run with directory-service
 *                                overrides deactivated.
 */
function allowDirectoriesDuring(innerTask) {
  return async () => {
    DownloadIntegration.allowDirectories = true;
    try {
      return await innerTask();
    } finally {
      DownloadIntegration.allowDirectories = false;
    }
  };
}

/**
 * Check that `DownloadIntegration` and the `nsIExternalHelperAppService` agree
 * concerning the value of `getPreferredDownloadsDirectory()`.
 */
add_task(
  {
    // nsIExternalHelperAppService (q.v.) doesn't implement this on Android
    skip_if: AppConstants.platform === "android",
  },
  allowDirectoriesDuring(
    async function test_preferredDownloadsDirectoryMatches() {
      const integrationDirectory =
        await DownloadIntegration.getPreferredDownloadsDirectory();
      Assert.ok(
        !integrationDirectory || typeof integrationDirectory === "string"
      );

      /** @type nsIExternalHelperAppService */
      const extHelperAppSvc = Cc[
        "@mozilla.org/uriloader/external-helper-app-service;1"
      ].getService(Ci.nsIExternalHelperAppService);

      const externalHelperDirectory =
        extHelperAppSvc.getPreferredDownloadsDirectory();
      Assert.ok(
        !externalHelperDirectory ||
          externalHelperDirectory.QueryInterface(Ci.nsIFile)
      );

      Assert.equal(!externalHelperDirectory, !integrationDirectory);
      if (externalHelperDirectory) {
        Assert.equal(externalHelperDirectory.path, integrationDirectory);
      }
    }
  )
);

/**
 * Confirm that the mocking layer has been reenabled after the previous test.
 */
add_task(async function test_allowDirectoriesIsOnlyLocallySet() {
  Assert.ok(!DownloadIntegration.allowDirectories);
  Assert.equal(
    DownloadIntegration._getDirectory("TmpD"),
    DownloadIntegration._getDirectory("Home")
  );
});

/**
 * Checks that application reputation blocks the download and the target file
 * does not exist.
 */
add_task(async function test_blocked_applicationReputation() {
  let download = await promiseBlockedDownload({
    keepPartialData: false,
    keepBlockedData: false,
  });

  // Now that the download is blocked, the target file should not exist.
  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);

  // There should also be no blocked data in this case
  Assert.ok(!download.hasBlockedData);
});

/**
 * Checks that if a download restarts while processing an application reputation
 * request, the status is handled correctly.
 */
add_task(async function test_blocked_applicationReputation_race() {
  let isFirstShouldBlockCall = true;

  let blockFn = () => ({
    shouldBlockForReputationCheck(download) {
      if (isFirstShouldBlockCall) {
        isFirstShouldBlockCall = false;

        // 2. Cancel and restart the download before the first attempt has a
        //    chance to finish.
        download.cancel();
        download.removePartialData();
        download.start();

        // 3. Allow the first attempt to finish with a blocked response.
        return Promise.resolve({
          shouldBlock: true,
          verdict: Ci.nsIApplicationReputationService.VERDICT_UNCOMMON,
        });
      }

      // 4/5. Don't block the download the second time. The race condition would
      //      occur with the first attempt regardless of whether the second one
      //      is blocked, but not blocking here makes the test simpler.
      return Promise.resolve({
        shouldBlock: false,
        verdict: Ci.nsIApplicationReputationService.VERDICT_SAFE,
      });
    },
    shouldKeepBlockedData: () => Promise.resolve(true),
  });

  Integration.downloads.register(blockFn);
  function cleanup() {
    Integration.downloads.unregister(blockFn);
  }
  registerCleanupFunction(cleanup);

  let download;

  try {
    // 1. Start the download and get a reference to the promise asociated with
    //    the first attempt, before allowing the response to continue.
    download = await promiseStartDownload_tryToKeepPartialData({
      useLegacySaver: gUseLegacySaver,
    });
    let firstAttempt = promiseDownloadStopped(download);
    continueResponses();

    // 4/5. Wait for the first attempt to be completed. The result of this
    //      should appear as a cancellation.
    await firstAttempt;

    do_throw("The first attempt should have been canceled.");
  } catch (ex) {
    // The "becauseBlocked" property should be false.
    if (!(ex instanceof Downloads.Error) || ex.becauseBlocked) {
      throw ex;
    }
  }

  // 6. Wait for the second attempt to be completed.
  await promiseDownloadStopped(download);

  // 7. At this point, "hasBlockedData" should be false.
  Assert.ok(!download.hasBlockedData);

  cleanup();
});

/**
 * Checks that application reputation blocks the download but maintains the
 * blocked data, which will be deleted when the block is confirmed.
 */
add_task(async function test_blocked_applicationReputation_confirmBlock() {
  let download = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
  });

  Assert.ok(download.hasBlockedData);
  Assert.equal((await IOUtils.stat(download.target.path)).size, 0);
  Assert.ok(await IOUtils.exists(download.target.partFilePath));

  await download.confirmBlock();

  // After confirming the block the download should be in a failed state and
  // have no downloaded data left on disk.
  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);
  Assert.ok(!download.hasBlockedData);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);
});

/**
 * Checks that confirmBlock sets deleted to true.
 */
add_task(
  async function test_blocked_applicationReputation_confirmBlock_sets_deleted() {
    let download = await promiseBlockedDownload({
      keepPartialData: true,
      keepBlockedData: true,
    });

    Assert.ok(download.hasBlockedData);
    Assert.ok(!download.deleted);

    await download.confirmBlock();

    Assert.ok(!download.hasBlockedData);
    Assert.ok(download.deleted);
  }
);

/**
 * Checks that application reputation blocks the download but maintains the
 * blocked data, which will be used to complete the download when unblocking.
 */
add_task(async function test_blocked_applicationReputation_unblock() {
  let download = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
    useLegacySaver: gUseLegacySaver,
  });

  Assert.ok(download.hasBlockedData);
  Assert.equal((await IOUtils.stat(download.target.path)).size, 0);
  Assert.ok(await IOUtils.exists(download.target.partFilePath));

  await download.unblock();

  // After unblocking the download should have succeeded and be
  // present at the final path.
  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.hasBlockedData);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  await promiseVerifyTarget(download.target, TEST_DATA_SHORT + TEST_DATA_SHORT);

  // The only indication the download was previously blocked is the
  // existence of the error, so we make sure it's still set.
  Assert.ok(download.error instanceof Downloads.Error);
  Assert.ok(download.error.becauseBlocked);
  Assert.ok(download.error.becauseBlockedByReputationCheck);
});

/**
 * Check that calling cancel on a blocked download will not cause errors
 */
add_task(async function test_blocked_applicationReputation_cancel() {
  let download = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
  });

  // This call should succeed on a blocked download.
  await download.cancel();

  // Calling cancel should not have changed the current state, the download
  // should still be blocked.
  Assert.ok(download.error.becauseBlockedByReputationCheck);
  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);
  Assert.ok(download.hasBlockedData);
});

/**
 * Checks that unblock and confirmBlock cannot race on a blocked download
 */
add_task(async function test_blocked_applicationReputation_decisionRace() {
  let download = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
  });

  let unblockPromise = download.unblock();
  let confirmBlockPromise = download.confirmBlock();

  await confirmBlockPromise.then(
    () => {
      do_throw("confirmBlock should have failed.");
    },
    () => {}
  );

  await unblockPromise;

  // After unblocking the download should have succeeded and be
  // present at the final path.
  Assert.ok(download.stopped);
  Assert.ok(download.succeeded);
  Assert.ok(!download.hasBlockedData);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  Assert.ok(await IOUtils.exists(download.target.path));

  download = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
  });

  confirmBlockPromise = download.confirmBlock();
  unblockPromise = download.unblock();

  await unblockPromise.then(
    () => {
      do_throw("unblock should have failed.");
    },
    () => {}
  );

  await confirmBlockPromise;

  // After confirming the block the download should be in a failed state and
  // have no downloaded data left on disk.
  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);
  Assert.ok(!download.hasBlockedData);
  Assert.equal(false, await IOUtils.exists(download.target.partFilePath));
  Assert.equal(false, await IOUtils.exists(download.target.path));
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);
});

/**
 * Checks that unblocking a blocked download fails if the blocked data has been
 * removed.
 */
add_task(async function test_blocked_applicationReputation_unblock() {
  let download = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
  });

  Assert.ok(download.hasBlockedData);
  Assert.ok(await IOUtils.exists(download.target.partFilePath));

  // Remove the blocked data without telling the download.
  await IOUtils.remove(download.target.partFilePath);

  let unblockPromise = download.unblock();
  await unblockPromise.then(
    () => {
      do_throw("unblock should have failed.");
    },
    () => {}
  );

  // Even though unblocking failed the download state should have been updated
  // to reflect the lack of blocked data.
  Assert.ok(!download.hasBlockedData);
  Assert.ok(download.stopped);
  Assert.ok(!download.succeeded);
  Assert.ok(!download.target.exists);
  Assert.equal(download.target.size, 0);
});

/**
 * download.showContainingDirectory() action
 */
add_task(async function test_showContainingDirectory() {
  let targetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

  let download = await Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: "",
  });

  let promiseDirectoryShown = waitForDirectoryShown();
  await download.showContainingDirectory();
  let path = await promiseDirectoryShown;
  try {
    new FileUtils.File(path);
    do_throw("Should have failed because of an invalid path.");
  } catch (ex) {
    if (!(ex instanceof Components.Exception)) {
      throw ex;
    }
    // Invalid paths on Windows are reported with NS_ERROR_FAILURE,
    // but with NS_ERROR_FILE_UNRECOGNIZED_PATH on Mac/Linux
    let validResult =
      ex.result == Cr.NS_ERROR_FILE_UNRECOGNIZED_PATH ||
      ex.result == Cr.NS_ERROR_FAILURE;
    Assert.ok(validResult);
  }

  download = await Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: targetPath,
  });

  promiseDirectoryShown = waitForDirectoryShown();
  download.showContainingDirectory();
  await promiseDirectoryShown;
});

/**
 * download.launch() action with launcherPath
 */
add_task(async function test_launch() {
  let customLauncher = getTempFile("app-launcher");

  // Test both with and without setting a custom application.
  for (let launcherPath of [null, customLauncher.path]) {
    let download;
    if (!gUseLegacySaver) {
      // When testing DownloadCopySaver, we have control over the download, thus
      // we can test that file is not launched if download.succeeded is not set.
      download = await Downloads.createDownload({
        source: httpUrl("source.txt"),
        target: getTempFile(TEST_TARGET_FILE_NAME).path,
        launcherPath,
        launchWhenSucceeded: true,
      });

      try {
        await download.launch();
        do_throw("Can't launch download file as it has not completed yet");
      } catch (ex) {
        Assert.equal(
          ex.message,
          "launch can only be called if the download succeeded"
        );
      }

      Assert.ok(download.launchWhenSucceeded);
      await download.start();
    } else {
      // When testing DownloadLegacySaver, the download is already started when
      // it is created, thus we don't test calling "launch" before starting.
      download = await promiseStartLegacyDownload(httpUrl("source.txt"), {
        launcherPath,
        launchWhenSucceeded: true,
      });
      Assert.ok(download.launchWhenSucceeded);
      await promiseDownloadStopped(download);
    }

    let promiseFileLaunched = waitForFileLaunched();
    download.launch();
    let result = await promiseFileLaunched;

    // Verify that the results match the test case.
    if (!launcherPath) {
      // This indicates that the default handler has been chosen.
      Assert.strictEqual(result, null);
    } else {
      // Check the nsIMIMEInfo instance that would have been used for launching.
      Assert.equal(result.preferredAction, Ci.nsIMIMEInfo.useHelperApp);
      Assert.ok(
        result.preferredApplicationHandler
          .QueryInterface(Ci.nsILocalHandlerApp)
          .executable.equals(customLauncher)
      );
    }
  }
});

/**
 * download.launch() action with launcherId
 */
add_task(
  {
    skip_if: () => mozinfo.os != "linux", // This test is relevant only for Linux.
  },
  async function test_launch_id() {
    let customLauncherId = "org.gnome.gedit.desktop";

    // Test both with and without setting a custom application.
    for (let launcherId of [null, customLauncherId]) {
      let download;
      if (!gUseLegacySaver) {
        // When testing DownloadCopySaver, we have control over the download, thus
        // we can test that file is not launched if download.succeeded is not set.
        download = await Downloads.createDownload({
          source: httpUrl("source.txt"),
          target: getTempFile(TEST_TARGET_FILE_NAME).path,
          launcherId,
          launchWhenSucceeded: true,
        });

        try {
          await download.launch();
          do_throw("Can't launch download file as it has not completed yet");
        } catch (ex) {
          Assert.equal(
            ex.message,
            "launch can only be called if the download succeeded"
          );
        }

        Assert.ok(download.launchWhenSucceeded);
        await download.start();
      } else {
        // When testing DownloadLegacySaver, the download is already started when
        // it is created, thus we don't test calling "launch" before starting.
        download = await promiseStartLegacyDownload(httpUrl("source.txt"), {
          launcherId,
          launchWhenSucceeded: true,
        });
        Assert.ok(download.launchWhenSucceeded);
        await promiseDownloadStopped(download);
      }

      let promiseFileLaunched = waitForFileLaunched();
      download.launch();
      let result = await promiseFileLaunched;
      // Verify that the results match the test case.
      if (!launcherId) {
        // This indicates that the default handler has been chosen.
        Assert.strictEqual(result, null);
      } else {
        // Check the nsIMIMEInfo instance that would have been used for launching.
        let launcher = Cc["@mozilla.org/gio-service;1"]
          .getService(Ci.nsIGIOService)
          .createHandlerAppFromAppId(launcherId);

        Assert.equal(result.preferredAction, Ci.nsIMIMEInfo.useHelperApp);
        Assert.ok(
          result.preferredApplicationHandler
            .QueryInterface(Ci.nsIGIOHandlerApp)
            .equals(launcher)
        );
      }
    }
  }
);

/**
 * Test passing an invalid path as the launcherPath property.
 */
add_task(async function test_launcherPath_invalid() {
  let download = await Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: { path: getTempFile(TEST_TARGET_FILE_NAME).path },
    launcherPath: " ",
  });

  let promiseDownloadLaunched = new Promise(resolve => {
    let waitFn = base => {
      let launchOverride = {
        launchDownload() {
          Integration.downloads.unregister(waitFn);
          let superPromise = super.launchDownload(...arguments);
          resolve(superPromise);
          return superPromise;
        },
      };
      Object.setPrototypeOf(launchOverride, base);
      return launchOverride;
    };
    Integration.downloads.register(waitFn);
  });

  await download.start();
  try {
    download.launch();
    await promiseDownloadLaunched;
    do_throw("Can't launch file with invalid custom launcher");
  } catch (ex) {
    if (!(ex instanceof Components.Exception)) {
      throw ex;
    }
    // Invalid paths on Windows are reported with NS_ERROR_FAILURE,
    // but with NS_ERROR_FILE_UNRECOGNIZED_PATH on Mac/Linux
    let validResult =
      ex.result == Cr.NS_ERROR_FILE_UNRECOGNIZED_PATH ||
      ex.result == Cr.NS_ERROR_FAILURE;
    Assert.ok(validResult);
  }
});

/**
 * Test passing an invalid id as the launcherId property.
 */
add_task(
  {
    skip_if: () => mozinfo.os != "linux", // This test is relevant only for Linux.
  },
  async function test_launcherId_invalid() {
    let download = await Downloads.createDownload({
      source: { url: httpUrl("source.txt") },
      target: { path: getTempFile(TEST_TARGET_FILE_NAME).path },
      launcherId: " ",
    });
    todo_check_true(false, "Size should not be zero.");

    let promiseDownloadLaunched = new Promise(resolve => {
      let waitFn = base => {
        let launchOverride = {
          launchDownload() {
            Integration.downloads.unregister(waitFn);
            let superPromise = super.launchDownload(...arguments);
            resolve(superPromise);
            return superPromise;
          },
        };
        Object.setPrototypeOf(launchOverride, base);
        return launchOverride;
      };
      Integration.downloads.register(waitFn);
    });

    await download.start();
    try {
      download.launch();
      await promiseDownloadLaunched;
      do_throw("Can't launch file with invalid custom launcher");
    } catch (ex) {
      if (!(ex instanceof Components.Exception)) {
        throw ex;
      }
      let validResult = ex.result == Cr.NS_ERROR_FAILURE;
      Assert.ok(validResult);
    }
  }
);

/**
 * Tests that download.launch() is automatically called after
 * the download finishes if download.launchWhenSucceeded = true
 */
add_task(async function test_launchWhenSucceeded() {
  let customLauncher = getTempFile("app-launcher");

  // Test both with and without setting a custom application.
  for (let launcherPath of [null, customLauncher.path]) {
    let promiseFileLaunched = waitForFileLaunched();

    if (!gUseLegacySaver) {
      let download = await Downloads.createDownload({
        source: httpUrl("source.txt"),
        target: getTempFile(TEST_TARGET_FILE_NAME).path,
        launchWhenSucceeded: true,
        launcherPath,
      });
      await download.start();
    } else {
      let download = await promiseStartLegacyDownload(httpUrl("source.txt"), {
        launcherPath,
        launchWhenSucceeded: true,
      });
      await promiseDownloadStopped(download);
    }

    let result = await promiseFileLaunched;

    // Verify that the results match the test case.
    if (!launcherPath) {
      // This indicates that the default handler has been chosen.
      Assert.strictEqual(result, null);
    } else {
      // Check the nsIMIMEInfo instance that would have been used for launching.
      Assert.equal(result.preferredAction, Ci.nsIMIMEInfo.useHelperApp);
      Assert.ok(
        result.preferredApplicationHandler
          .QueryInterface(Ci.nsILocalHandlerApp)
          .executable.equals(customLauncher)
      );
    }
  }
});

/**
 * Tests that download.launch() is automatically called after
 * the download finishes if download.launchWhenSucceeded = true
 *
 * This is version when using launcherId property instead of launcherPath.
 */
add_task(
  {
    skip_if: () => mozinfo.os != "linux", // This test is relevant only for Linux.
  },
  async function test_launchWhenSucceeded() {
    let customLauncherId = "org.gnome.gedit.desktop";

    // Test both with and without setting a custom application.
    for (let launcherId of [null, customLauncherId]) {
      let promiseFileLaunched = waitForFileLaunched();

      if (!gUseLegacySaver) {
        let download = await Downloads.createDownload({
          source: httpUrl("source.txt"),
          target: getTempFile(TEST_TARGET_FILE_NAME).path,
          launchWhenSucceeded: true,
          launcherId,
        });
        await download.start();
      } else {
        let download = await promiseStartLegacyDownload(httpUrl("source.txt"), {
          launcherId,
          launchWhenSucceeded: true,
        });
        await promiseDownloadStopped(download);
      }

      let result = await promiseFileLaunched;

      // Verify that the results match the test case.
      if (!launcherId) {
        // This indicates that the default handler has been chosen.
        Assert.strictEqual(result, null, "Result expected to be null");
      } else {
        // Check the nsIMIMEInfo instance that would have been used for launching.
        Assert.equal(result.preferredAction, Ci.nsIMIMEInfo.useHelperApp);
        let launcher = Cc["@mozilla.org/gio-service;1"]
          .getService(Ci.nsIGIOService)
          .createHandlerAppFromAppId(launcherId);
        Assert.ok(
          result.preferredApplicationHandler
            .QueryInterface(Ci.nsIGIOHandlerApp)
            .equals(launcher)
        );
      }
    }
  }
);

/**
 * Tests that the proper content type is set for a normal download.
 */
add_task(async function test_contentType() {
  let download = await promiseStartDownload(httpUrl("source.txt"));
  await promiseDownloadStopped(download);

  Assert.equal("text/plain", download.contentType);
});

/**
 * Tests that the serialization/deserialization of the startTime Date
 * object works correctly.
 */
add_task(async function test_toSerializable_startTime() {
  let download1 = await promiseStartDownload(httpUrl("source.txt"));
  await promiseDownloadStopped(download1);

  let serializable = download1.toSerializable();
  let reserialized = JSON.parse(JSON.stringify(serializable));

  let download2 = await Downloads.createDownload(reserialized);

  Assert.equal(download1.startTime.constructor.name, "Date");
  Assert.equal(download2.startTime.constructor.name, "Date");
  Assert.equal(download1.startTime.toJSON(), download2.startTime.toJSON());
});

/**
 * Checks that downloads are added to browsing history when they start.
 */
add_task(async function test_history() {
  mustInterruptResponses();

  let sourceUrl = httpUrl("interruptible.txt");

  // We will wait for the visit to be notified during the download.
  await PlacesUtils.history.clear();
  let promiseVisit = promiseWaitForVisit(sourceUrl);

  // Start a download that is not allowed to finish yet.
  let download = await promiseStartDownload(sourceUrl);
  let expectedFile = new FileUtils.File(download.target.path);
  let expectedFileURI = Services.io.newFileURI(expectedFile);
  let promiseAnnotation = waitForAnnotation(
    sourceUrl,
    "downloads/destinationFileURI",
    expectedFileURI.spec
  );

  // The history and annotation notifications should be received before the download completes.
  let [time, transitionType, lastKnownTitle] = await promiseVisit;
  await promiseAnnotation;

  Assert.equal(time, download.startTime.getTime());
  Assert.equal(transitionType, Ci.nsINavHistoryService.TRANSITION_DOWNLOAD);
  Assert.equal(lastKnownTitle, expectedFile.leafName);

  let pageInfo = await PlacesUtils.history.fetch(sourceUrl, {
    includeAnnotations: true,
  });
  Assert.equal(
    pageInfo.annotations.get("downloads/destinationFileURI"),
    expectedFileURI.spec,
    "Should have saved the correct download target annotation."
  );

  // Restart and complete the download after clearing history.
  await PlacesUtils.history.clear();
  download.cancel();
  continueResponses();
  await download.start();

  // The restart should not have added a new history visit.
  Assert.equal(false, await PlacesUtils.history.hasVisits(sourceUrl));
});

/**
 * Checks that downloads started by nsIHelperAppService are added to the
 * browsing history when they start.
 */
add_task(async function test_history_tryToKeepPartialData() {
  // We will wait for the visit to be notified during the download.
  await PlacesUtils.history.clear();
  let promiseVisit = promiseWaitForVisit(
    httpUrl("interruptible_resumable.txt")
  );

  // Start a download that is not allowed to finish yet.
  let beforeStartTimeMs = Date.now();
  let download = await promiseStartDownload_tryToKeepPartialData({
    useLegacySaver: gUseLegacySaver,
  });

  // The history notifications should be received before the download completes.
  let [time, transitionType] = await promiseVisit;
  Assert.equal(transitionType, Ci.nsINavHistoryService.TRANSITION_DOWNLOAD);

  // The time set by nsIHelperAppService may be different than the start time in
  // the download object, thus we only check that it is a meaningful time.  Note
  // that we subtract one second from the earliest time to account for rounding.
  Assert.greaterOrEqual(time, beforeStartTimeMs - 1000);

  // Complete the download before finishing the test.
  continueResponses();
  await promiseDownloadStopped(download);
});

/**
 * Checks that finished downloads are not removed.
 */
add_task(async function test_download_cancel_retry_finalize() {
  // Start a download that is not allowed to finish yet.
  let sourceUrl = httpUrl("interruptible.txt");
  let targetFilePath = getTempFile(TEST_TARGET_FILE_NAME).path;
  mustInterruptResponses();
  let download1 = await Downloads.createDownload({
    source: sourceUrl,
    target: { path: targetFilePath, partFilePath: targetFilePath + ".part" },
  });
  download1.start().catch(() => {});
  await promiseDownloadMidway(download1);
  await promisePartFileReady(download1);

  // Cancel the download and make sure that the partial data do not exist.
  await download1.cancel();
  Assert.equal(targetFilePath, download1.target.path);
  Assert.equal(false, await IOUtils.exists(download1.target.path));
  Assert.equal(false, await IOUtils.exists(download1.target.partFilePath));
  continueResponses();

  // Download the same file again with a different download session.
  let download2 = await Downloads.createDownload({
    source: sourceUrl,
    target: { path: targetFilePath, partFilePath: targetFilePath + ".part" },
  });
  download2.start().catch(() => {});

  // Wait for download to be completed.
  await promiseDownloadStopped(download2);
  Assert.equal(targetFilePath, download2.target.path);
  Assert.ok(await IOUtils.exists(download2.target.path));
  Assert.equal(false, await IOUtils.exists(download2.target.partFilePath));

  // Finalize the first download session.
  await download1.finalize(true);

  // The complete download should not have been removed.
  Assert.ok(await IOUtils.exists(download2.target.path));
  Assert.equal(false, await IOUtils.exists(download2.target.partFilePath));
});

/**
 * Checks that confirmBlock does not clobber unrelated safe files.
 */
add_task(async function test_blocked_removeByHand_confirmBlock() {
  let download1 = await promiseBlockedDownload({
    keepPartialData: true,
    keepBlockedData: true,
  });

  Assert.ok(download1.hasBlockedData);
  Assert.equal((await IOUtils.stat(download1.target.path)).size, 0);
  Assert.ok(await IOUtils.exists(download1.target.partFilePath));

  // Remove the placeholder without telling the download.
  await IOUtils.remove(download1.target.path);
  Assert.equal(false, await IOUtils.exists(download1.target.path));

  // Download a file with the same name as the blocked download.
  let download2 = await Downloads.createDownload({
    source: httpUrl("interruptible_resumable.txt"),
    target: {
      path: download1.target.path,
      partFilePath: download1.target.path + ".part",
    },
  });
  download2.start().catch(() => {});

  // Wait for download to be completed.
  await promiseDownloadStopped(download2);
  Assert.equal(download1.target.path, download2.target.path);
  Assert.ok(await IOUtils.exists(download2.target.path));

  // Remove the blocked download.
  await download1.confirmBlock();

  // After confirming the complete download should not have been removed.
  Assert.ok(await IOUtils.exists(download2.target.path));
});

/**
 * Tests that the temp download files are removed on exit and exiting private
 * mode after they have been launched.
 */
add_task(async function test_launchWhenSucceeded_deleteTempFileOnExit() {
  let customLauncherPath = getTempFile("app-launcher").path;
  let autoDeleteTargetPathOne = getTempFile(TEST_TARGET_FILE_NAME).path;
  let autoDeleteTargetPathTwo = getTempFile(TEST_TARGET_FILE_NAME).path;
  let noAutoDeleteTargetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

  Services.prefs.setBoolPref(kDeletePrivateFileFeature, false);
  let autoDeleteDownloadOne = await Downloads.createDownload({
    source: { url: httpUrl("source.txt"), isPrivate: true },
    target: autoDeleteTargetPathOne,
    launchWhenSucceeded: true,
    launcherPath: customLauncherPath,
  });
  await autoDeleteDownloadOne.start();

  Services.prefs.setBoolPref(kDeleteTempFileOnExit, true);
  let autoDeleteDownloadTwo = await Downloads.createDownload({
    source: httpUrl("source.txt"),
    target: autoDeleteTargetPathTwo,
    launchWhenSucceeded: true,
    launcherPath: customLauncherPath,
  });
  await autoDeleteDownloadTwo.start();

  Services.prefs.setBoolPref(kDeleteTempFileOnExit, false);
  let noAutoDeleteDownload = await Downloads.createDownload({
    source: httpUrl("source.txt"),
    target: noAutoDeleteTargetPath,
    launchWhenSucceeded: true,
    launcherPath: customLauncherPath,
  });
  await noAutoDeleteDownload.start();

  Services.prefs.clearUserPref(kDeleteTempFileOnExit);

  Assert.ok(
    await IOUtils.exists(autoDeleteTargetPathOne),
    "Auto-delete target one should exist"
  );
  Assert.ok(
    await IOUtils.exists(autoDeleteTargetPathTwo),
    "Auto-delete target two should exist"
  );
  Assert.ok(
    await IOUtils.exists(noAutoDeleteTargetPath),
    "No auto-delete target should exist"
  );

  // Simulate leaving private browsing mode
  Services.obs.notifyObservers(null, "last-pb-context-exited");
  Assert.equal(
    false,
    await IOUtils.exists(autoDeleteTargetPathOne),
    "Auto-delete target one should not exist after exiting pb"
  );
  Services.prefs.clearUserPref(kDeletePrivateFileFeature);

  // Simulate browser shutdown
  let expire = Cc[
    "@mozilla.org/uriloader/external-helper-app-service;1"
  ].getService(Ci.nsIObserver);
  expire.observe(null, "profile-before-change", null);

  // The file should still exist following the simulated shutdown.
  Assert.ok(
    await IOUtils.exists(autoDeleteTargetPathTwo),
    "Auto-delete target two should exist"
  );
  Assert.ok(
    await IOUtils.exists(noAutoDeleteTargetPath),
    "No auto-delete target should exist"
  );
});

/**
 * Tests that the temp download files are removed on exit and exiting private
 * mode after they have been launched.
 *
 * This is version when using launcherId property instead of launcherPath.
 */
add_task(
  async function test_launchWhenSucceeded_deleteTempFileOnExit_with_launcherId() {
    let customLauncherId = "org.gnome.gedit.desktop";
    let autoDeleteTargetPathOne = getTempFile(TEST_TARGET_FILE_NAME).path;
    let autoDeleteTargetPathTwo = getTempFile(TEST_TARGET_FILE_NAME).path;
    let noAutoDeleteTargetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

    Services.prefs.setBoolPref(kDeletePrivateFileFeature, false);

    let autoDeleteDownloadOne = await Downloads.createDownload({
      source: { url: httpUrl("source.txt"), isPrivate: true },
      target: autoDeleteTargetPathOne,
      launchWhenSucceeded: true,
      launcherId: customLauncherId,
    });
    await autoDeleteDownloadOne.start();

    Services.prefs.setBoolPref(kDeleteTempFileOnExit, true);
    let autoDeleteDownloadTwo = await Downloads.createDownload({
      source: httpUrl("source.txt"),
      target: autoDeleteTargetPathTwo,
      launchWhenSucceeded: true,
      launcherId: customLauncherId,
    });
    await autoDeleteDownloadTwo.start();

    Services.prefs.setBoolPref(kDeleteTempFileOnExit, false);
    let noAutoDeleteDownload = await Downloads.createDownload({
      source: httpUrl("source.txt"),
      target: noAutoDeleteTargetPath,
      launchWhenSucceeded: true,
      launcherId: customLauncherId,
    });
    await noAutoDeleteDownload.start();

    Services.prefs.clearUserPref(kDeleteTempFileOnExit);

    Assert.ok(
      await IOUtils.exists(autoDeleteTargetPathOne),
      "Auto-delete target one should exist"
    );
    Assert.ok(
      await IOUtils.exists(autoDeleteTargetPathTwo),
      "Auto-delete target two should exist"
    );
    Assert.ok(
      await IOUtils.exists(noAutoDeleteTargetPath),
      "No auto-delete target should exist"
    );

    // Simulate leaving private browsing mode
    Services.obs.notifyObservers(null, "last-pb-context-exited");
    Assert.equal(
      false,
      await IOUtils.exists(autoDeleteTargetPathOne),
      "Auto-delete target one should not exist after exiting pb"
    );

    Services.prefs.clearUserPref(kDeletePrivateFileFeature);

    // Simulate browser shutdown
    let expire = Cc[
      "@mozilla.org/uriloader/external-helper-app-service;1"
    ].getService(Ci.nsIObserver);
    expire.observe(null, "profile-before-change", null);

    // The file should still exist following the simulated shutdown.
    Assert.ok(await IOUtils.exists(autoDeleteTargetPathTwo));
    Assert.ok(await IOUtils.exists(noAutoDeleteTargetPath));
  }
);

add_task(async function test_partitionKey() {
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);

  function promiseVerifyDownloadChannel(url, partitionKey) {
    return TestUtils.topicObserved("http-on-modify-request", subject => {
      let httpChannel = subject.QueryInterface(Ci.nsIHttpChannel);
      if (httpChannel.URI.spec != url) {
        return false;
      }

      let reqLoadInfo = httpChannel.loadInfo;
      let cookieJarSettings = reqLoadInfo.cookieJarSettings;

      // Check the partitionKey of the cookieJarSettings.
      Assert.equal(cookieJarSettings.partitionKey, partitionKey);

      return true;
    });
  }

  let test_url = httpUrl("source.txt");
  let uri = Services.io.newURI(test_url);
  let cookieJarSettings = Cc["@mozilla.org/cookieJarSettings;1"].createInstance(
    Ci.nsICookieJarSettings
  );
  cookieJarSettings.initWithURI(uri, false);
  let expectedPartitionKey = cookieJarSettings.partitionKey;

  let verifyPromise;

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can check its basic properties before it starts.
    download = await Downloads.createDownload({
      source: { url: test_url, cookieJarSettings },
      target: { path: targetFile.path },
      saver: { type: "copy" },
    });

    Assert.equal(download.source.url, test_url);
    Assert.equal(download.target.path, targetFile.path);

    verifyPromise = promiseVerifyDownloadChannel(
      test_url,
      expectedPartitionKey
    );

    await download.start();
  } else {
    verifyPromise = promiseVerifyDownloadChannel(
      test_url,
      expectedPartitionKey
    );

    // When testing DownloadLegacySaver, the download is already started when it
    // is created, thus we must check its basic properties while in progress.
    download = await promiseStartLegacyDownload(null, {
      targetFile,
      cookieJarSettings,
    });

    Assert.equal(download.source.url, test_url);
    Assert.equal(download.target.path, targetFile.path);

    await promiseDownloadStopped(download);
  }

  await verifyPromise;
});

add_task(async function test_isInCurrentBatch() {
  let i = 0;
  function promiseControlledDownload(len) {
    const path = `controlled_${i++}.txt`;
    const sourcePath = `/${path}`;
    return new Promise(function (resolve) {
      promiseNewDownload(httpUrl(path)).then(download => {
        registerCleanupFunction(() =>
          gHttpServer.registerPathHandler(sourcePath, null)
        );
        gHttpServer.registerPathHandler(
          sourcePath,
          function (aRequest, aResponse) {
            info(`Controlled ${path} request started.`);

            aResponse.processAsync();
            aResponse.setHeader("Content-Type", "text/plain", false);
            aResponse.setHeader("Content-Length", "" + len, false);

            let remaining = len;
            resolve(
              Object.assign(download, {
                advance(amount) {
                  aResponse.write("a".repeat(amount));
                  remaining -= amount;
                  if (remaining <= 0) {
                    aResponse.finish();
                    info(`Controlled ${path} request finished.`);
                  }
                },
              })
            );
          }
        );
        download.start();
      });
    });
  }

  const dls = [];
  function assertCurrentBatch(...expected) {
    Assert.deepEqual(
      dls.map(d => d.isInCurrentBatch),
      expected
    );
  }
  dls.push(await promiseControlledDownload(100));
  dls[0].advance(50);
  await promiseDownloadMidway(dls[0]);

  assertCurrentBatch(true);

  dls.push(await promiseControlledDownload(100));
  dls[1].advance(50);
  await promiseDownloadMidway(dls[1]);

  assertCurrentBatch(true, true);

  dls[0].advance(50);
  await promiseDownloadFinished(dls[0]);

  assertCurrentBatch(true, true);

  dls.push(await promiseControlledDownload(100));
  dls[1].cancel();
  dls[1].advance(50);
  dls[2].advance(50);
  await promiseDownloadMidway(dls[2]);

  assertCurrentBatch(true, false, true);

  dls[2].advance(50);
  await promiseDownloadFinished(dls[2]);

  assertCurrentBatch(false, false, false);

  dls.push(await promiseControlledDownload(100));
  dls[3].advance(50);
  await promiseDownloadMidway(dls[3]);

  assertCurrentBatch(false, false, false, true);

  dls[3].advance(50);
  await promiseDownloadFinished(dls[3]);

  assertCurrentBatch(false, false, false, false);
});
