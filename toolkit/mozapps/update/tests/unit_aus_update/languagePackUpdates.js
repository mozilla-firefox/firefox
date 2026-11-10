const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { getAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);
const { XPIExports } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/XPIExports.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

AddonTestUtils.init(this);
setupTestCommon();
AddonTestUtils.appInfo = getAppInfo();
start_httpserver();
setUpdateURL(gURLData + gHTTPHandlerPath);
setUpdateChannel("test_channel");
Services.prefs.setBoolPref(PREF_APP_UPDATE_LANGPACK_ENABLED, true);

/**
 * Returns a promise that will resolve when the add-ons manager attempts to
 * stage langpack updates. The returned object contains the appVersion and
 * platformVersion parameters as well as resolve and reject functions to
 * complete the mocked langpack update.
 */
function mockLangpackUpdate() {
  let stagingCall = Promise.withResolvers();
  XPIExports.XPIInstall.stageLangpacksForAppUpdate = (
    appVersion,
    platformVersion
  ) => {
    let result = Promise.withResolvers();
    stagingCall.resolve({
      appVersion,
      platformVersion,
      resolve: result.resolve,
      reject: result.reject,
    });

    return result.promise;
  };

  return stagingCall.promise;
}

add_setup(async function () {
  Services.fog.initializeFOG();

  await AddonTestUtils.promiseStartupManager();
});

add_task(async function testLangpackUpdateSuccess() {
  Services.fog.testResetFOG();

  let updateDownloadNotified = false;
  let notified = waitForEvent("update-downloaded").then(
    () => (updateDownloadNotified = true)
  );

  let stagingCall = mockLangpackUpdate();

  await downloadUpdate();

  // We have to wait for UpdateService's onStopRequest to run far enough that
  // the notification will have been sent if the language pack update completed.
  await TestUtils.waitForCondition(() => readStatusFile() == "pending");

  Assert.ok(
    !updateDownloadNotified,
    "Should not have seen the notification yet."
  );

  let { appVersion, platformVersion, resolve } = await stagingCall;
  Assert.equal(
    appVersion,
    DEFAULT_UPDATE_VERSION,
    "Should see the right app version"
  );
  Assert.equal(
    platformVersion,
    DEFAULT_UPDATE_VERSION,
    "Should see the right platform version"
  );

  resolve();

  await notified;

  // Because we resolved the lang pack call after the download completed a value
  // should have been recorded in telemetry.
  Assert.greater(
    Glean.update.langpackOvertime.testGetValue()?.count ?? 0,
    0,
    "Should have recorded a time"
  );

  // Reload the update manager so that we can download the same update again
  await reloadUpdateManagerData(true);
});

add_task(async function testLangpackUpdateFails() {
  let updateDownloadNotified = false;
  let notified = waitForEvent("update-downloaded").then(
    () => (updateDownloadNotified = true)
  );

  let stagingCall = mockLangpackUpdate();

  await downloadUpdate();

  // We have to wait for UpdateService's onStopRequest to run far enough that
  // the notification will have been sent if the language pack update completed.
  await TestUtils.waitForCondition(() => readStatusFile() == "pending");

  Assert.ok(
    !updateDownloadNotified,
    "Should not have seen the notification yet."
  );

  let { appVersion, platformVersion, reject } = await stagingCall;
  Assert.equal(
    appVersion,
    DEFAULT_UPDATE_VERSION,
    "Should see the right app version"
  );
  Assert.equal(
    platformVersion,
    DEFAULT_UPDATE_VERSION,
    "Should see the right platform version"
  );

  reject();

  await notified;

  // Reload the update manager so that we can download the same update again
  await reloadUpdateManagerData(true);
});

add_task(async function testLangpackStaged() {
  let updateStagedNotified = false;
  let notified = waitForEvent("update-staged").then(
    () => (updateStagedNotified = true)
  );

  let stagingCall = mockLangpackUpdate();

  Services.prefs.setBoolPref(PREF_APP_UPDATE_STAGING_ENABLED, true);
  await setupTestAppBundle();

  let greDir = getGREDir();

  if (AppConstants.platform != "macosx") {
    let updateSettingsIni = greDir.clone();
    updateSettingsIni.append(FILE_UPDATE_SETTINGS_INI);
    writeFile(updateSettingsIni, UPDATE_SETTINGS_CONTENTS);
  }

  await downloadUpdate();

  // We have to wait for the update to be applied and then check that the
  // notification hasn't been sent. Staging runs the updater in a separate
  // process, so allow considerably more time than waitForCondition's default
  // five seconds.
  try {
    await TestUtils.waitForCondition(
      () => readStatusFile() == "applied",
      "Waiting for the update to be staged",
      /* interval */ 500,
      /* maxTries */ 60
    );
  } catch (e) {
    logTestInfo(`the update status is "${readStatusFile()}"`);
    logUpdateLog(FILE_UPDATE_LOG);
    throw e;
  }

  Assert.ok(
    !updateStagedNotified,
    "Should not have seen the notification yet."
  );

  let { appVersion, platformVersion, resolve } = await stagingCall;
  Assert.equal(
    appVersion,
    DEFAULT_UPDATE_VERSION,
    "Should see the right app version"
  );
  Assert.equal(
    platformVersion,
    DEFAULT_UPDATE_VERSION,
    "Should see the right platform version"
  );

  resolve();

  await notified;

  // Reload the update manager so that we can download the same update again
  await reloadUpdateManagerData(true);
});

add_task(async function testRedownload() {
  // When the download of a partial mar fails the same downloader is re-used to
  // download the complete mar. We should only call the add-ons manager to stage
  // language packs once in this case.
  Services.prefs.setBoolPref(PREF_APP_UPDATE_STAGING_ENABLED, false);
  Services.fog.testResetFOG();

  let partialPatch = getRemotePatchString({
    type: "partial",
    url: gURLData + "missing.mar",
    size: 28,
  });
  let completePatch = getRemotePatchString({});
  let updateString = getRemoteUpdateString({}, partialPatch + completePatch);
  gResponseBody = getRemoteUpdatesXMLString(updateString);

  let { updates } = await waitForUpdateCheck(true);

  initMockIncrementalDownload();
  gIncrementalDownloadErrorType = 3;

  let stageCount = 0;
  XPIExports.XPIInstall.stageLangpacksForAppUpdate = () => {
    stageCount++;
    return Promise.resolve();
  };

  let downloadCount = 0;
  let listener = {
    onStartRequest: _aRequest => {},
    onProgress: (_aRequest, _aContext, _aProgress, _aMaxProgress) => {},
    onStatus: (_aRequest, _aStatus, _aStatusText) => {},
    onStopRequest: (request, status) => {
      Assert.equal(
        status,
        downloadCount ? 0 : Cr.NS_ERROR_CORRUPTED_CONTENT,
        "Should have seen the right status."
      );
      downloadCount++;

      // Keep the same status.
      gIncrementalDownloadErrorType = 3;
    },
    QueryInterface: ChromeUtils.generateQI([
      "nsIRequestObserver",
      "nsIProgressEventSink",
    ]),
  };
  gAUS.addDownloadListener(listener);

  let bestUpdate = await gAUS.selectUpdate(updates);
  await gAUS.downloadUpdate(bestUpdate, false);

  await waitForEvent("update-downloaded");

  gAUS.removeDownloadListener(listener);

  Assert.equal(downloadCount, 2, "Should have seen two downloads");
  Assert.equal(stageCount, 1, "Should have only tried to stage langpacks once");

  // Because we resolved the lang pack call before the download completed a value
  // should not have been recorded in telemetry.
  Assert.equal(
    Glean.update.langpackOvertime.testGetValue()?.count ?? 0,
    0,
    "Should not have recorded a time"
  );

  // Reload the update manager so that we can download the same update again
  await reloadUpdateManagerData(true);
});

add_task(async function finish() {
  stop_httpserver(doTestFinish);
});
