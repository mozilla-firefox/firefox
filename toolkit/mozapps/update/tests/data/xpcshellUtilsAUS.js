/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test log warnings that happen before the test has started
 * "Couldn't get the user appdata directory. Crash events may not be produced."
 * in nsExceptionHandler.cpp (possibly bug 619104)
 *
 * "XPCOM objects created/destroyed from static ctor/dtor" in nsTraceRefcnt.cpp
 * (possibly bug 457479)
 *
 * Other warnings printed to the test logs
 * "site security information will not be persisted" in
 * nsSiteSecurityService.cpp and the error in nsSystemInfo.cpp preceding this
 * error are due to not having a profile when running some of the xpcshell
 * tests. Since most xpcshell tests also log these errors these tests don't
 * call do_get_profile unless necessary for the test.
 * "!mMainThread" in nsThreadManager.cpp are due to using timers and it might be
 * possible to fix some or all of these in the test itself.
 * "NS_FAILED(rv)" in nsThreadUtils.cpp are due to using timers and it might be
 * possible to fix some or all of these in the test itself.
 */

"use strict";

const EXIT_CODE_BASE = ChromeUtils.importESModule(
  "resource://gre/modules/BackgroundTasksManager.sys.mjs"
).EXIT_CODE;
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { Subprocess } = ChromeUtils.importESModule(
  "resource://gre/modules/Subprocess.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  MockRegistrar: "resource://testing-common/MockRegistrar.sys.mjs",
  updateAppInfo: "resource://testing-common/AppInfo.sys.mjs",
});

const Cm = Components.manager;

/* global MOZ_APP_VENDOR, MOZ_APP_BASENAME */
/* global MOZ_VERIFY_MAR_SIGNATURE, IS_AUTHENTICODE_CHECK_ENABLED */
load("../data/xpcshellConstantsPP.js");

// Note: DIR_CONTENTS, DIR_MACOS and DIR_RESOURCES only differ on macOS. They
//       default to "" on all other platforms.
const DIR_CONTENTS = AppConstants.platform == "macosx" ? "Contents/" : "";
const DIR_MACOS =
  AppConstants.platform == "macosx" ? DIR_CONTENTS + "MacOS/" : "";
const DIR_RESOURCES =
  AppConstants.platform == "macosx" ? DIR_CONTENTS + "Resources/" : "";
const TEST_FILE_SUFFIX = AppConstants.platform == "macosx" ? "_mac" : "";
const FILE_COMPLETE_MAR = "complete" + TEST_FILE_SUFFIX + ".mar";
const FILE_PARTIAL_MAR = "partial" + TEST_FILE_SUFFIX + ".mar";
const FILE_COMPLETE_PRECOMPLETE = "complete_precomplete" + TEST_FILE_SUFFIX;
const FILE_PARTIAL_PRECOMPLETE = "partial_precomplete" + TEST_FILE_SUFFIX;
const FILE_COMPLETE_REMOVEDFILES = "complete_removed-files" + TEST_FILE_SUFFIX;
const FILE_PARTIAL_REMOVEDFILES = "partial_removed-files" + TEST_FILE_SUFFIX;
const FILE_UPDATE_IN_PROGRESS_LOCK = "updated.update_in_progress.lock";
const COMPARE_LOG_SUFFIX = "_" + mozinfo.os;
const LOG_COMPLETE_SUCCESS = "complete_log_success" + COMPARE_LOG_SUFFIX;
const LOG_PARTIAL_SUCCESS = "partial_log_success" + COMPARE_LOG_SUFFIX;
const LOG_PARTIAL_FAILURE = "partial_log_failure" + COMPARE_LOG_SUFFIX;
const LOG_REPLACE_SUCCESS = "replace_log_success";
const MAC_APP_XATTR_KEY = "com.apple.application-instance";
const MAC_APP_XATTR_VALUE = "dlsource%3Dmozillaci";

const USE_EXECV = AppConstants.platform == "linux";

const URL_HOST = "http://localhost";

const APP_INFO_NAME = "XPCShell";
const APP_INFO_VENDOR = "Mozilla";

const APP_BIN_SUFFIX =
  AppConstants.platform == "linux" ? "-bin" : mozinfo.bin_suffix;
const FILE_APP_BIN = AppConstants.MOZ_APP_NAME + APP_BIN_SUFFIX;
const FILE_COMPLETE_EXE = "complete.exe";
const FILE_HELPER_BIN =
  AppConstants.platform == "macosx"
    ? "callback_app.app/Contents/MacOS/TestAUSHelper"
    : "TestAUSHelper" + mozinfo.bin_suffix;
const FILE_HELPER_APP =
  AppConstants.platform == "macosx" ? "callback_app.app" : FILE_HELPER_BIN;
const FILE_MAINTENANCE_SERVICE_BIN = "maintenanceservice.exe";
const FILE_MAINTENANCE_SERVICE_INSTALLER_BIN =
  "maintenanceservice_installer.exe";
const FILE_OLD_VERSION_MAR = "old_version.mar";
const FILE_PARTIAL_EXE = "partial.exe";
const FILE_UPDATER_BIN =
  "updater" + (AppConstants.platform == "macosx" ? ".app" : mozinfo.bin_suffix);

const PERFORMING_STAGED_UPDATE = "Performing a staged update";
const CALL_QUIT = "calling QuitProgressUI";
const ERR_UPDATE_IN_PROGRESS = "Update already in progress! Exiting";
const ERR_RENAME_FILE = "rename_file: failed to rename file";
const ERR_ENSURE_COPY = "ensure_copy: failed to copy the file";
const ERR_UNABLE_OPEN_DEST = "unable to open destination file";
const ERR_BACKUP_DISCARD = "backup_discard: unable to remove";
const ERR_MOVE_DESTDIR_7 = "Moving destDir to tmpDir failed, err: 7";
const ERR_BACKUP_CREATE_7 = "backup_create failed: 7";
const ERR_LOADSOURCEFILE_FAILED = "LoadSourceFile failed";
const ERR_PARENT_PID_PERSISTS =
  "The parent process didn't exit! Continuing with update.";
const ERR_BGTASK_EXCLUSIVE =
  "failed to exclusively open executable file from background task: ";

const LOG_SVC_SUCCESSFUL_LAUNCH = "Process was started... waiting on result.";
const LOG_SVC_UNSUCCESSFUL_LAUNCH =
  "The install directory path is not valid for this application.";

// Typical end of a message when calling assert
const MSG_SHOULD_EQUAL = " should equal the expected value";
const MSG_SHOULD_EXIST = "the file or directory should exist";
const MSG_SHOULD_NOT_EXIST = "the file or directory should not exist";

const CONTINUE_CHECK = "continueCheck";
const CONTINUE_DOWNLOAD = "continueDownload";
const CONTINUE_STAGING = "continueStaging";

// Time in seconds the helper application should sleep before exiting. The
// helper can also be made to exit by writing |finish| to its input file.
const HELPER_SLEEP_TIMEOUT = 180;

// How many of do_timeout calls using FILE_IN_USE_TIMEOUT_MS to wait before the
// test is aborted.
const FILE_IN_USE_TIMEOUT_MS = 1000;

const PIPE_TO_NULL =
  AppConstants.platform == "win" ? ">nul" : "> /dev/null 2>&1";

const LOG_FUNCTION = info;

const gHTTPHandlerPath = "updates.xml";

var gIsServiceTest;
var gTestID;

// This default value will be overridden when using the http server.
var gURLData = URL_HOST + "/";
var gTestserver;
var gUpdateCheckCount = 0;

const REL_PATH_DATA = "";
const APP_UPDATE_SJS_HOST = "http://127.0.0.1";
const APP_UPDATE_SJS_PATH = "/" + REL_PATH_DATA + "app_update.sjs";

var gIncrementalDownloadErrorType;

var gResponseBody;

var gProcess;
var gAppTimer;
var gHandle;

var gGREDirOrig;
var gGREBinDirOrig;

var gPIDPersistProcess;

// Variables are used instead of contants so tests can override these values if
// necessary.
var gCallbackArgs = ["./", "callback.log", "Test Arg 2", "Test Arg 3"];
var gCallbackApp = (() => {
  if (AppConstants.platform == "macosx") {
    return "callback_app.app";
  }
  return "callback_app" + mozinfo.bin_suffix;
})();

var gCallbackBinFile = (() => {
  if (AppConstants.platform == "macosx") {
    return FILE_HELPER_BIN;
  }
  return "callback_app" + mozinfo.bin_suffix;
})();

var gPostUpdateBinFile = "postup_app" + mozinfo.bin_suffix;

var gTimeoutRuns = 0;

// Environment related globals
var gShouldResetEnv = undefined;
var gAddedEnvXRENoWindowsCrashDialog = false;
var gCrashReporterDisabled;
var gEnvXPCOMDebugBreak;
var gEnvXPCOMMemLeakLog;
var gEnvForceServiceFallback = false;

const URL_HTTP_UPDATE_SJS = "http://test_details/";
const DATA_URI_SPEC = Services.io.newFileURI(do_get_file("", false)).spec;

/* import-globals-from shared.js */
load("shared.js");

// Set to true to log additional information for debugging. To log additional
// information for individual tests set gDebugTest to false here and to true in
// the test's onload function.
gDebugTest = true;

// Setting gDebugTestLog to true will create log files for the tests in
// <objdir>/_tests/xpcshell/toolkit/mozapps/update/tests/<testdir>/ except for
// the service tests since they run sequentially. This can help when debugging
// failures for the tests that intermittently fail when they run in parallel.
// Never set gDebugTestLog to true except when running tests locally.
var gDebugTestLog = false;
// An empty array for gTestsToLog will log most of the output of all of the
// update tests except for the service tests. To only log specific tests add the
// test file name without the file extension to the array below.
var gTestsToLog = [];
var gRealDump;
var gFOS;
var gUpdateBin;

var gTestFiles = [];
var gTestDirs = [];

// Common files for both successful and failed updates.
var gTestFilesCommon = [
  {
    description: "Should never change",
    fileName: FILE_CHANNEL_PREFS,
    relPathDir:
      AppConstants.platform == "macosx"
        ? "Contents/Frameworks/ChannelPrefs.framework/"
        : DIR_RESOURCES + "defaults/pref/",
    originalContents: "ShouldNotBeReplaced\n",
    compareContents: "ShouldNotBeReplaced\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o767,
    comparePerms: 0o767,
  },
];

var gTestFilesCommonNonMac = [
  {
    description: "Should never change",
    fileName: FILE_UPDATE_SETTINGS_INI,
    relPathDir: DIR_RESOURCES,
    originalContents: UPDATE_SETTINGS_CONTENTS,
    compareContents: UPDATE_SETTINGS_CONTENTS,
    originalFile: null,
    compareFile: null,
    originalPerms: 0o767,
    comparePerms: 0o767,
  },
];

if (AppConstants.platform != "macosx") {
  gTestFilesCommon = gTestFilesCommon.concat(gTestFilesCommonNonMac);
}

var gTestFilesCommonMac = [
  {
    description: "Should never change",
    fileName: FILE_UPDATE_SETTINGS_FRAMEWORK,
    relPathDir:
      DIR_MACOS + "updater.app/Contents/Frameworks/UpdateSettings.framework/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
    existingFile: true,
  },
  {
    description: "Should never change",
    fileName: FILE_INFO_PLIST,
    relPathDir: DIR_CONTENTS,
    originalContents: DIR_APP_INFO_PLIST_FILE_CONTENTS,
    compareContents: DIR_APP_INFO_PLIST_FILE_CONTENTS,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
    existingFile: true,
  },
  {
    description: "Should never change",
    fileName: FILE_INFO_PLIST,
    relPathDir: DIR_MACOS + "updater.app/Contents/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
    existingFile: true,
  },
  {
    description: "Should never change",
    fileName: FILE_INFO_PLIST,
    relPathDir: DIR_MACOS + "callback_app.app/Contents/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
    existingFile: true,
  },
];

if (AppConstants.platform == "macosx") {
  gTestFilesCommon = gTestFilesCommon.concat(gTestFilesCommonMac);
}

// Files for a complete successful update. This can be used for a complete
// failed update by calling setTestFilesAndDirsForFailure.
var gTestFilesCompleteSuccess = [
  {
    description: "Added by update.manifest (add)",
    fileName: "precomplete",
    relPathDir: DIR_RESOURCES,
    originalContents: null,
    compareContents: null,
    originalFile: FILE_PARTIAL_PRECOMPLETE,
    compareFile: FILE_COMPLETE_PRECOMPLETE,
    originalPerms: 0o666,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "searchpluginstext0",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: "ToBeReplacedWithFromComplete\n",
    compareContents: "FromComplete\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o775,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "searchpluginspng1.png",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: "complete.png",
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "searchpluginspng0.png",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: null,
    compareContents: null,
    originalFile: "partial.png",
    compareFile: "complete.png",
    originalPerms: 0o666,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "removed-files",
    relPathDir: DIR_RESOURCES,
    originalContents: null,
    compareContents: null,
    originalFile: FILE_PARTIAL_REMOVEDFILES,
    compareFile: FILE_COMPLETE_REMOVEDFILES,
    originalPerms: 0o666,
    comparePerms: 0o644,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions1text0",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions1/",
    originalContents: null,
    compareContents: "FromComplete\n",
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions1png1.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions1/",
    originalContents: null,
    compareContents: null,
    originalFile: "partial.png",
    compareFile: "complete.png",
    originalPerms: 0o666,
    comparePerms: 0o644,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions1png0.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions1/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: "complete.png",
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions0text0",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions0/",
    originalContents: "ToBeReplacedWithFromComplete\n",
    compareContents: "FromComplete\n",
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions0png1.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions0/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: "complete.png",
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions0png0.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions0/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: "complete.png",
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "exe0.exe",
    relPathDir: DIR_MACOS,
    originalContents: null,
    compareContents: null,
    originalFile: FILE_HELPER_BIN,
    compareFile: FILE_COMPLETE_EXE,
    originalPerms: 0o777,
    comparePerms: 0o755,
    skipFileVerification: "macosx",
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "10text0",
    relPathDir: DIR_RESOURCES + "1/10/",
    originalContents: "ToBeReplacedWithFromComplete\n",
    compareContents: "FromComplete\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o767,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "0exe0.exe",
    relPathDir: DIR_RESOURCES + "0/",
    originalContents: null,
    compareContents: null,
    originalFile: FILE_HELPER_BIN,
    compareFile: FILE_COMPLETE_EXE,
    originalPerms: 0o777,
    comparePerms: 0o755,
    skipFileVerification: "macosx",
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "00text1",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: "ToBeReplacedWithFromComplete\n",
    compareContents: "FromComplete\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o677,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "00text0",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: "ToBeReplacedWithFromComplete\n",
    compareContents: "FromComplete\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o775,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "00png0.png",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: "complete.png",
    originalPerms: 0o776,
    comparePerms: 0o644,
  },
  {
    description: "Removed by precomplete (remove)",
    fileName: "20text0",
    relPathDir: DIR_RESOURCES + "2/20/",
    originalContents: "ToBeDeleted\n",
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
  },
  {
    description: "Removed by precomplete (remove)",
    fileName: "20png0.png",
    relPathDir: DIR_RESOURCES + "2/20/",
    originalContents: "ToBeDeleted\n",
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
  },
];

// Concatenate the common files to the end of the array.
gTestFilesCompleteSuccess = gTestFilesCompleteSuccess.concat(gTestFilesCommon);

// Files for a partial successful update. This can be used for a partial failed
// update by calling setTestFilesAndDirsForFailure.
var gTestFilesPartialSuccess = [
  {
    description: "Added by update.manifest (add)",
    fileName: "precomplete",
    relPathDir: DIR_RESOURCES,
    originalContents: null,
    compareContents: null,
    originalFile: FILE_COMPLETE_PRECOMPLETE,
    compareFile: FILE_PARTIAL_PRECOMPLETE,
    originalPerms: 0o666,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "searchpluginstext0",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: "ToBeReplacedWithFromPartial\n",
    compareContents: "FromPartial\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o775,
    comparePerms: 0o644,
  },
  {
    description: "Patched by update.manifest if the file exists (patch-if)",
    fileName: "searchpluginspng1.png",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o666,
    comparePerms: 0o666,
  },
  {
    description: "Patched by update.manifest if the file exists (patch-if)",
    fileName: "searchpluginspng0.png",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o666,
    comparePerms: 0o666,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions1text0",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions1/",
    originalContents: null,
    compareContents: "FromPartial\n",
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description:
      "Patched by update.manifest if the parent directory exists (patch-if)",
    fileName: "extensions1png1.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions1/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o666,
    comparePerms: 0o666,
  },
  {
    description:
      "Patched by update.manifest if the parent directory exists (patch-if)",
    fileName: "extensions1png0.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions1/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o666,
    comparePerms: 0o666,
  },
  {
    description:
      "Added by update.manifest if the parent directory exists (add-if)",
    fileName: "extensions0text0",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions0/",
    originalContents: "ToBeReplacedWithFromPartial\n",
    compareContents: "FromPartial\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o644,
    comparePerms: 0o644,
  },
  {
    description:
      "Patched by update.manifest if the parent directory exists (patch-if)",
    fileName: "extensions0png1.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions0/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o644,
    comparePerms: 0o644,
  },
  {
    description:
      "Patched by update.manifest if the parent directory exists (patch-if)",
    fileName: "extensions0png0.png",
    relPathDir: DIR_RESOURCES + "distribution/extensions/extensions0/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o644,
    comparePerms: 0o644,
  },
  {
    description: "Patched by update.manifest (patch)",
    fileName: "exe0.exe",
    relPathDir: DIR_MACOS,
    originalContents: null,
    compareContents: null,
    originalFile: FILE_COMPLETE_EXE,
    compareFile: FILE_PARTIAL_EXE,
    originalPerms: 0o755,
    comparePerms: 0o755,
    skipFileVerification: "macosx",
  },
  {
    description: "Patched by update.manifest (patch)",
    fileName: "0exe0.exe",
    relPathDir: DIR_RESOURCES + "0/",
    originalContents: null,
    compareContents: null,
    originalFile: FILE_COMPLETE_EXE,
    compareFile: FILE_PARTIAL_EXE,
    originalPerms: 0o755,
    comparePerms: 0o755,
    skipFileVerification: "macosx",
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "00text0",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: "ToBeReplacedWithFromPartial\n",
    compareContents: "FromPartial\n",
    originalFile: null,
    compareFile: null,
    originalPerms: 0o644,
    comparePerms: 0o644,
  },
  {
    description: "Patched by update.manifest (patch)",
    fileName: "00png0.png",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: null,
    compareContents: null,
    originalFile: "complete.png",
    compareFile: "partial.png",
    originalPerms: 0o666,
    comparePerms: 0o666,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "20text0",
    relPathDir: DIR_RESOURCES + "2/20/",
    originalContents: null,
    compareContents: "FromPartial\n",
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "20png0.png",
    relPathDir: DIR_RESOURCES + "2/20/",
    originalContents: null,
    compareContents: null,
    originalFile: null,
    compareFile: "partial.png",
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description: "Added by update.manifest (add)",
    fileName: "00text2",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: null,
    compareContents: "FromPartial\n",
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: 0o644,
  },
  {
    description: "Removed by update.manifest (remove)",
    fileName: "10text0",
    relPathDir: DIR_RESOURCES + "1/10/",
    originalContents: "ToBeDeleted\n",
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
  },
  {
    description: "Removed by update.manifest (remove)",
    fileName: "00text1",
    relPathDir: DIR_RESOURCES + "0/00/",
    originalContents: "ToBeDeleted\n",
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
  },
];

// Concatenate the common files to the end of the array.
gTestFilesPartialSuccess = gTestFilesPartialSuccess.concat(gTestFilesCommon);

/**
 * Searches `gTestFiles` for the file with the given filename. This is currently
 * not very efficient (it searches the whole array every time).
 *
 * @param filename
 *        The name of the file to search for (i.e. the `fileName` attribute).
 * @returns
 *        The object in `gTestFiles` that describes the requested file.
 *        Or `null`, if the file is not in `gTestFiles`.
 */
function getTestFileByName(filename) {
  return gTestFiles.find(f => f.fileName == filename) ?? null;
}

var gTestDirsCommon = [
  {
    relPathDir: DIR_RESOURCES + "3/",
    dirRemoved: false,
    files: ["3text0", "3text1"],
    filesRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "4/",
    dirRemoved: true,
    files: ["4text0", "4text1"],
    filesRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "5/",
    dirRemoved: true,
    files: ["5test.exe", "5text0", "5text1"],
    filesRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "6/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "7/",
    dirRemoved: true,
    files: ["7text0", "7text1"],
    subDirs: ["70/", "71/"],
    subDirFiles: ["7xtest.exe", "7xtext0", "7xtext1"],
  },
  {
    relPathDir: DIR_RESOURCES + "8/",
    dirRemoved: false,
  },
  {
    relPathDir: DIR_RESOURCES + "8/80/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "8/81/",
    dirRemoved: false,
    files: ["81text0", "81text1"],
  },
  {
    relPathDir: DIR_RESOURCES + "8/82/",
    dirRemoved: false,
    subDirs: ["820/", "821/"],
  },
  {
    relPathDir: DIR_RESOURCES + "8/83/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "8/84/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "8/85/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "8/86/",
    dirRemoved: true,
    files: ["86text0", "86text1"],
  },
  {
    relPathDir: DIR_RESOURCES + "8/87/",
    dirRemoved: true,
    subDirs: ["870/", "871/"],
    subDirFiles: ["87xtext0", "87xtext1"],
  },
  {
    relPathDir: DIR_RESOURCES + "8/88/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "8/89/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "9/90/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "9/91/",
    dirRemoved: false,
    files: ["91text0", "91text1"],
  },
  {
    relPathDir: DIR_RESOURCES + "9/92/",
    dirRemoved: false,
    subDirs: ["920/", "921/"],
  },
  {
    relPathDir: DIR_RESOURCES + "9/93/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "9/94/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "9/95/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "9/96/",
    dirRemoved: true,
    files: ["96text0", "96text1"],
  },
  {
    relPathDir: DIR_RESOURCES + "9/97/",
    dirRemoved: true,
    subDirs: ["970/", "971/"],
    subDirFiles: ["97xtext0", "97xtext1"],
  },
  {
    relPathDir: DIR_RESOURCES + "9/98/",
    dirRemoved: true,
  },
  {
    relPathDir: DIR_RESOURCES + "9/99/",
    dirRemoved: true,
  },
  {
    description:
      "Silences 'WARNING: Failed to resolve XUL App Dir.' in debug builds",
    relPathDir: DIR_RESOURCES + "browser",
    dirRemoved: false,
  },
];

// Directories for a complete successful update. This array can be used for a
// complete failed update by calling setTestFilesAndDirsForFailure.
var gTestDirsCompleteSuccess = [
  {
    description: "Removed by precomplete (rmdir)",
    relPathDir: DIR_RESOURCES + "2/20/",
    dirRemoved: true,
  },
  {
    description: "Removed by precomplete (rmdir)",
    relPathDir: DIR_RESOURCES + "2/",
    dirRemoved: true,
  },
];

// Concatenate the common files to the beginning of the array.
gTestDirsCompleteSuccess = gTestDirsCommon.concat(gTestDirsCompleteSuccess);

// Directories for a partial successful update. This array can be used for a
// partial failed update by calling setTestFilesAndDirsForFailure.
var gTestDirsPartialSuccess = [
  {
    description: "Removed by update.manifest (rmdir)",
    relPathDir: DIR_RESOURCES + "1/10/",
    dirRemoved: true,
  },
  {
    description: "Removed by update.manifest (rmdir)",
    relPathDir: DIR_RESOURCES + "1/",
    dirRemoved: true,
  },
];

// Concatenate the common files to the beginning of the array.
gTestDirsPartialSuccess = gTestDirsCommon.concat(gTestDirsPartialSuccess);

/**
 * Helper function for setting up the test environment.
 *
 * @param  aAppUpdateAutoEnabled
 *         See setAppUpdateAutoSync in shared.js for details.
 * @param  aAllowBits
 *         If true, allow update downloads via the Windows BITS service.
 *         If false, this download mechanism will not be used.
 */
function setupTestCommon(aAppUpdateAutoEnabled = false, aAllowBits = false) {
  debugDump("start - general test setup");

  Assert.strictEqual(
    gTestID,
    undefined,
    "gTestID should be 'undefined' (setupTestCommon should " +
      "only be called once)"
  );

  let caller = Components.stack.caller;
  gTestID = caller.filename.toString().split("/").pop().split(".")[0];

  if (gDebugTestLog && !gIsServiceTest) {
    if (!gTestsToLog.length || gTestsToLog.includes(gTestID)) {
      let logFile = do_get_file(gTestID + ".log", true);
      if (!logFile.exists()) {
        logFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
      }
      gFOS = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
        Ci.nsIFileOutputStream
      );
      gFOS.init(logFile, MODE_WRONLY | MODE_APPEND, PERMS_FILE, 0);

      gRealDump = dump;
      dump = dumpOverride;
    }
  }

  createAppInfo("xpcshell@tests.mozilla.org", APP_INFO_NAME, "1.0", "2.0");

  if (gIsServiceTest && !shouldRunServiceTest()) {
    return false;
  }

  do_test_pending();

  setDefaultPrefs();

  gGREDirOrig = getGREDir();
  gGREBinDirOrig = getGREBinDir();

  let applyDir = getApplyDirFile().parent;

  // Try to remove the directory used to apply updates and the updates directory
  // on platforms other than Windows. This is non-fatal for the test since if
  // this fails a different directory will be used.
  if (applyDir.exists()) {
    debugDump("attempting to remove directory. Path: " + applyDir.path);
    try {
      removeDirRecursive(applyDir);
    } catch (e) {
      logTestInfo(
        "non-fatal error removing directory. Path: " +
          applyDir.path +
          ", Exception: " +
          e
      );
      // When the application doesn't exit properly it can cause the test to
      // fail again on the second run with an NS_ERROR_FILE_ACCESS_DENIED error
      // along with no useful information in the test log. To prevent this use
      // a different directory for the test when it isn't possible to remove the
      // existing test directory (bug 1294196).
      gTestID += "_new";
      logTestInfo(
        "using a new directory for the test by changing gTestID " +
          "since there is an existing test directory that can't be " +
          "removed, gTestID: " +
          gTestID
      );
    }
  }

  if (AppConstants.platform == "win") {
    Services.prefs.setBoolPref(
      PREF_APP_UPDATE_SERVICE_ENABLED,
      !!gIsServiceTest
    );
  }

  if (gIsServiceTest) {
    let exts = ["id", "log", "status"];
    for (let i = 0; i < exts.length; ++i) {
      let file = getSecureOutputFile(exts[i]);
      if (file.exists()) {
        try {
          file.remove(false);
        } catch (e) {}
      }
    }
  }

  adjustGeneralPaths();
  createWorldWritableAppUpdateDir();

  // Logged once here instead of in the mock directory provider to lessen test
  // log spam.
  debugDump("Updates Directory (UpdRootD) Path: " + getMockUpdRootD().path);

  // This prevents a warning about not being able to find the greprefs.js file
  // from being logged.
  let grePrefsFile = getGREDir();
  if (!grePrefsFile.exists()) {
    grePrefsFile.create(Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
  }
  grePrefsFile.append("greprefs.js");
  if (!grePrefsFile.exists()) {
    grePrefsFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
  }

  // The name of the update lock needs to be changed to match the path
  // overridden in adjustGeneralPaths() above. Wait until now to reset
  // because the GRE dir now exists, which may cause the "install
  // path" to be normalized differently now that it can be resolved.
  debugDump("resetting update lock");
  resetSyncManagerLock();

  // Remove the updates directory on Windows and macOS which is located
  // outside of the application directory after the call to adjustGeneralPaths
  // has set it up. Since the test hasn't run yet, the directory shouldn't
  // exist and failure to remove the directory should be non-fatal for the test.
  if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
    let updatesDir = getMockUpdRootD();
    if (updatesDir.exists()) {
      debugDump("attempting to remove directory. Path: " + updatesDir.path);
      try {
        removeDirRecursive(updatesDir);
      } catch (e) {
        logTestInfo(
          "non-fatal error removing directory. Path: " +
            updatesDir.path +
            ", Exception: " +
            e
        );
      }
    }
  }

  setAppUpdateAutoSync(aAppUpdateAutoEnabled);
  Services.prefs.setBoolPref(PREF_APP_UPDATE_BITS_ENABLED, aAllowBits);

  debugDump("finish - general test setup");
  return true;
}

/**
 * Nulls out the most commonly used global vars used by tests to prevent leaks
 * as needed and attempts to restore the system to its original state.
 */
function cleanupTestCommon() {
  debugDump("start - general test cleanup");

  if (gChannel) {
    gPrefRoot.removeObserver(PREF_APP_UPDATE_CHANNEL, observer);
  }

  gTestserver = null;

  if (AppConstants.platform == "macosx" || AppConstants.platform == "linux") {
    // This will delete the launch script if it exists.
    getLaunchScript();
  }

  if (gIsServiceTest) {
    let exts = ["id", "log", "status"];
    for (let i = 0; i < exts.length; ++i) {
      let file = getSecureOutputFile(exts[i]);
      if (file.exists()) {
        try {
          file.remove(false);
        } catch (e) {}
      }
    }
  }

  if (AppConstants.platform == "win" && MOZ_APP_BASENAME) {
    let appDir = getApplyDirFile();
    let vendor = MOZ_APP_VENDOR ? MOZ_APP_VENDOR : "Mozilla";
    const REG_PATH =
      "SOFTWARE\\" + vendor + "\\" + MOZ_APP_BASENAME + "\\TaskBarIDs";
    let key = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );
    try {
      key.open(
        Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
        REG_PATH,
        Ci.nsIWindowsRegKey.ACCESS_ALL
      );
      if (key.hasValue(appDir.path)) {
        key.removeValue(appDir.path);
      }
    } catch (e) {}
    try {
      key.open(
        Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
        REG_PATH,
        Ci.nsIWindowsRegKey.ACCESS_ALL
      );
      if (key.hasValue(appDir.path)) {
        key.removeValue(appDir.path);
      }
    } catch (e) {}
  }

  // The updates directory is located outside of the application directory and
  // needs to be removed on Windows and Mac OS X.
  if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
    let updatesDir = getMockUpdRootD();
    // Try to remove the directory used to apply updates. Since the test has
    // already finished this is non-fatal for the test.
    if (updatesDir.exists()) {
      debugDump("attempting to remove directory. Path: " + updatesDir.path);
      try {
        removeDirRecursive(updatesDir);
      } catch (e) {
        logTestInfo(
          "non-fatal error removing directory. Path: " +
            updatesDir.path +
            ", Exception: " +
            e
        );
      }
      if (AppConstants.platform == "macosx") {
        let updatesRootDir = gUpdatesRootDir.clone();
        while (updatesRootDir.path != updatesDir.path) {
          if (updatesDir.exists()) {
            debugDump(
              "attempting to remove directory. Path: " + updatesDir.path
            );
            try {
              // Try to remove the directory without the recursive flag set
              // since the top level directory has already had its contents
              // removed and the parent directory might still be used by a
              // different test.
              updatesDir.remove(false);
            } catch (e) {
              logTestInfo(
                "non-fatal error removing directory. Path: " +
                  updatesDir.path +
                  ", Exception: " +
                  e
              );
              if (e == Cr.NS_ERROR_FILE_DIR_NOT_EMPTY) {
                break;
              }
            }
          }
          updatesDir = updatesDir.parent;
        }
      }
    }
  }

  let applyDir = getApplyDirFile().parent;

  // Try to remove the directory used to apply updates. Since the test has
  // already finished this is non-fatal for the test.
  if (applyDir.exists()) {
    debugDump("attempting to remove directory. Path: " + applyDir.path);
    try {
      removeDirRecursive(applyDir);
    } catch (e) {
      logTestInfo(
        "non-fatal error removing directory. Path: " +
          applyDir.path +
          ", Exception: " +
          e
      );
    }
  }
  // We just deleted this.
  gUpdateBin = null;

  resetEnvironment();
  Services.prefs.clearUserPref(PREF_APP_UPDATE_BITS_ENABLED);

  debugDump("finish - general test cleanup");

  if (gRealDump) {
    dump = gRealDump;
    gRealDump = null;
  }

  if (gFOS) {
    gFOS.close();
  }
}

/**
 * Helper function to store the log output of calls to dump in a variable so the
 * values can be written to a file for a parallel run of a test and printed to
 * the log file when the test runs synchronously.
 */
function dumpOverride(aText) {
  gFOS.write(aText, aText.length);
  gRealDump(aText);
}

/**
 * Helper function that calls do_test_finished that tracks whether a parallel
 * run of a test passed when it runs synchronously so the log output can be
 * inspected.
 */
async function doTestFinish() {
  if (gDebugTest) {
    // This prevents do_print errors from being printed by the xpcshell test
    // harness due to nsUpdateService.js logging to the console when the
    // app.update.log preference is true.
    Services.prefs.setBoolPref(PREF_APP_UPDATE_LOG, false);
    gAUS.observe(null, "nsPref:changed", PREF_APP_UPDATE_LOG);
  }

  await reloadUpdateManagerData(true);

  // Call app update's observe method passing quit-application to test that the
  // shutdown of app update runs without throwing or leaking. The observer
  // method is used directly instead of calling notifyObservers so components
  // outside of the scope of this test don't assert and thereby cause app update
  // tests to fail.
  gAUS.observe(null, "quit-application", "");

  executeSoon(do_test_finished);
}

/**
 * Sets the most commonly used preferences used by tests
 */
function setDefaultPrefs() {
  Services.prefs.setBoolPref(PREF_APP_UPDATE_DISABLEDFORTESTING, false);
  if (gDebugTest) {
    // Enable Update logging
    Services.prefs.setBoolPref(PREF_APP_UPDATE_LOG, true);
  } else {
    // Some apps set this preference to true by default
    Services.prefs.setBoolPref(PREF_APP_UPDATE_LOG, false);
  }
}

/**
 * Helper function for updater binary tests that sets the appropriate values
 * to check for update failures.
 */
function setTestFilesAndDirsForFailure() {
  gTestFiles.forEach(function STFADFF_Files(aTestFile) {
    aTestFile.compareContents = aTestFile.originalContents;
    aTestFile.compareFile = aTestFile.originalFile;
    aTestFile.comparePerms = aTestFile.originalPerms;
  });

  gTestDirs.forEach(function STFADFF_Dirs(aTestDir) {
    aTestDir.dirRemoved = false;
    if (aTestDir.filesRemoved) {
      aTestDir.filesRemoved = false;
    }
  });
}

/**
 * Helper function for updater binary tests that prevents the distribution
 * directory files from being created.
 */
function preventDistributionFiles() {
  gTestFiles = gTestFiles.filter(function (aTestFile) {
    return !aTestFile.relPathDir.includes("distribution/");
  });

  gTestDirs = gTestDirs.filter(function (aTestDir) {
    return !aTestDir.relPathDir.includes("distribution/");
  });
}

/**
 * On Mac OS X this sets the last modified time for the app bundle directory to
 * a date in the past to test that the last modified time is updated when an
 * update has been successfully applied (bug 600098).
 */
function setAppBundleModTime() {
  if (AppConstants.platform != "macosx") {
    return;
  }
  let now = Date.now();
  let yesterday = now - 1000 * 60 * 60 * 24;
  let applyToDir = getApplyDirFile();
  applyToDir.lastModifiedTime = yesterday;
}

/**
 * On Mac OS X this checks that the last modified time for the app bundle
 * directory has been updated when an update has been successfully applied
 * (bug 600098).
 */
function checkAppBundleModTime() {
  if (AppConstants.platform != "macosx") {
    return;
  }
  // All we care about is that the last modified time has changed so that Mac OS
  // X Launch Services invalidates its cache so the test allows up to one minute
  // difference in the last modified time.
  const MAC_MAX_TIME_DIFFERENCE = 60000;
  let now = Date.now();
  let applyToDir = getApplyDirFile();
  let timeDiff = Math.abs(applyToDir.lastModifiedTime - now);
  Assert.ok(
    timeDiff < MAC_MAX_TIME_DIFFERENCE,
    "the last modified time on the apply to directory should " +
      "change after a successful update"
  );
}

/**
 * Performs Update Manager checks to verify that the update metadata is correct
 * and that it is the same after the update xml files are reloaded.
 *
 * @param   aStatusFileState
 *          The expected state of the status file.
 * @param   aHasActiveUpdate
 *          Should there be an active update.
 * @param   aUpdateStatusState
 *          The expected update's status state.
 * @param   aUpdateErrCode
 *          The expected update's error code.
 * @param   aUpdateCount
 *          The update history's update count.
 */
async function checkUpdateManager(
  aStatusFileState,
  aHasActiveUpdate,
  aUpdateStatusState,
  aUpdateErrCode,
  aUpdateCount
) {
  let activeUpdate = await (aUpdateStatusState == STATE_DOWNLOADING
    ? gUpdateManager.getDownloadingUpdate()
    : gUpdateManager.getReadyUpdate());
  Assert.equal(
    readStatusState(),
    aStatusFileState,
    "the status file state" + MSG_SHOULD_EQUAL
  );
  let msgTags = [" after startup ", " after a file reload "];
  for (let i = 0; i < msgTags.length; ++i) {
    logTestInfo(
      "checking Update Manager updates" + msgTags[i] + "is performed"
    );
    if (aHasActiveUpdate) {
      Assert.ok(
        !!activeUpdate,
        msgTags[i] + "the active update should be defined"
      );
    } else {
      Assert.ok(
        !activeUpdate,
        msgTags[i] + "the active update should not be defined"
      );
    }
    const history = await gUpdateManager.getHistory();
    Assert.equal(
      history.length,
      aUpdateCount,
      msgTags[i] + "the update manager updateCount attribute" + MSG_SHOULD_EQUAL
    );
    if (aUpdateCount > 0) {
      let update = history[0];
      Assert.equal(
        update.state,
        aUpdateStatusState,
        msgTags[i] + "the first update state" + MSG_SHOULD_EQUAL
      );
      Assert.equal(
        update.errorCode,
        aUpdateErrCode,
        msgTags[i] + "the first update errorCode" + MSG_SHOULD_EQUAL
      );
    }
    if (i != msgTags.length - 1) {
      reloadUpdateManagerData();
    }
  }
}

/**
 * Waits until the update files exist or not based on the parameters specified
 * when calling this function or the default values if the parameters are not
 * specified. This is necessary due to the update xml files being written
 * asynchronously by nsIUpdateManager.
 *
 * @param   aActiveUpdateExists (optional)
 *          Whether the active-update.xml file should exist (default is false).
 * @param   aUpdatesExists (optional)
 *          Whether the updates.xml file should exist (default is true).
 */
async function waitForUpdateXMLFiles(
  aActiveUpdateExists = false,
  aUpdatesExists = true
) {
  function areFilesStabilized() {
    let file = getUpdateDirFile(FILE_ACTIVE_UPDATE_XML_TMP);
    if (file.exists()) {
      debugDump("file exists, Path: " + file.path);
      return false;
    }
    file = getUpdateDirFile(FILE_UPDATES_XML_TMP);
    if (file.exists()) {
      debugDump("file exists, Path: " + file.path);
      return false;
    }
    file = getUpdateDirFile(FILE_ACTIVE_UPDATE_XML);
    if (file.exists() != aActiveUpdateExists) {
      debugDump(
        "file exists should equal: " +
          aActiveUpdateExists +
          ", Path: " +
          file.path
      );
      return false;
    }
    file = getUpdateDirFile(FILE_UPDATES_XML);
    if (file.exists() != aUpdatesExists) {
      debugDump(
        "file exists should equal: " +
          aActiveUpdateExists +
          ", Path: " +
          file.path
      );
      return false;
    }
    return true;
  }

  await TestUtils.waitForCondition(
    () => areFilesStabilized(),
    "Waiting for update xml files to stabilize"
  );
}

/**
 * On Mac OS X and Windows this checks if the post update '.running' file exists
 * to determine if the post update binary was launched.
 *
 * @param   aShouldExist
 *          Whether the post update '.running' file should exist.
 */
function checkPostUpdateRunningFile(aShouldExist) {
  if (AppConstants.platform == "linux") {
    return;
  }
  let postUpdateRunningFile = getPostUpdateFile(".running");
  if (aShouldExist) {
    Assert.ok(
      postUpdateRunningFile.exists(),
      MSG_SHOULD_EXIST + getMsgPath(postUpdateRunningFile.path)
    );
  } else {
    Assert.ok(
      !postUpdateRunningFile.exists(),
      MSG_SHOULD_NOT_EXIST + getMsgPath(postUpdateRunningFile.path)
    );
  }
}

/**
 * Initializes the most commonly used settings and creates an instance of the
 * update service stub.
 */
async function standardInit() {
  // Initialize the update service stub component
  await initUpdateServiceStub();
}

/**
 * Helper function for getting the application version from the application.ini
 * file. This will look in both the GRE and the application directories for the
 * application.ini file.
 *
 * @return  The version string from the application.ini file.
 */
function getAppVersion() {
  // Read the application.ini and use its application version.
  let iniFile = gGREDirOrig.clone();
  iniFile.append(FILE_APPLICATION_INI);
  if (!iniFile.exists()) {
    iniFile = gGREBinDirOrig.clone();
    iniFile.append(FILE_APPLICATION_INI);
  }
  Assert.ok(iniFile.exists(), MSG_SHOULD_EXIST + getMsgPath(iniFile.path));
  let iniParser = Cc["@mozilla.org/xpcom/ini-parser-factory;1"]
    .getService(Ci.nsIINIParserFactory)
    .createINIParser(iniFile);
  return iniParser.getString("App", "Version");
}

/**
 * Helper function for getting the path to the directory where the
 * application binary is located (e.g. <test_file_leafname>/dir.app/).
 *
 * Note: The dir.app subdirectory under <test_file_leafname> is needed for
 *       platforms other than Mac OS X so the tests can run in parallel due to
 *       update staging creating a lock file named moz_update_in_progress.lock in
 *       the parent directory of the installation directory.
 * Note: For service tests with IS_AUTHENTICODE_CHECK_ENABLED we use an absolute
 *       path inside Program Files because the service itself will refuse to
 *       update an installation not located in Program Files.
 *
 * @return  The path to the directory where application binary is located.
 */
function getApplyDirPath() {
  if (gIsServiceTest && IS_AUTHENTICODE_CHECK_ENABLED) {
    let dir = getMaintSvcDir();
    dir.append(gTestID);
    dir.append("dir.app");
    return dir.path;
  }
  return gTestID + "/dir.app/";
}

/**
 * Helper function for getting the nsIFile for a file in the directory where the
 * update will be applied.
 *
 * The files for the update are located two directories below the apply to
 * directory since macOS sets the last modified time for the root directory
 * to the current time and if the update changes any files in the root directory
 * then it wouldn't be possible to test (bug 600098).
 *
 * @param   aRelPath (optional)
 *          The relative path to the file or directory to get from the root of
 *          the test's directory. If not specified the test's directory will be
 *          returned.
 * @return  The nsIFile for the file in the directory where the update will be
 *          applied.
 */
function getApplyDirFile(aRelPath) {
  // do_get_file only supports relative paths, but under these conditions we
  // need to use an absolute path in Program Files instead.
  if (gIsServiceTest && IS_AUTHENTICODE_CHECK_ENABLED) {
    let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    file.initWithPath(getApplyDirPath());
    if (aRelPath) {
      if (aRelPath == "..") {
        file = file.parent;
      } else {
        aRelPath = aRelPath.replace(/\//g, "\\");
        file.appendRelativePath(aRelPath);
      }
    }
    return file;
  }
  let relpath = getApplyDirPath() + (aRelPath ? aRelPath : "");
  return do_get_file(relpath, true);
}

/**
 * Helper function for getting the relative path to the directory where the
 * test data files are located.
 *
 * @return  The relative path to the directory where the test data files are
 *          located.
 */
function getTestDirPath() {
  return "../data/";
}

/**
 * Helper function for getting the nsIFile for a file in the test data
 * directory.
 *
 * @param   aRelPath (optional)
 *          The relative path to the file or directory to get from the root of
 *          the test's data directory. If not specified the test's data
 *          directory will be returned.
 * @param   aAllowNonExists (optional)
 *          Whether or not to throw an error if the path exists.
 *          If not specified, then false is used.
 * @return  The nsIFile for the file in the test data directory.
 * @throws  If the file or directory does not exist.
 */
function getTestDirFile(aRelPath, aAllowNonExists) {
  let relpath = getTestDirPath() + (aRelPath ? aRelPath : "");
  return do_get_file(relpath, !!aAllowNonExists);
}

/**
 * Helper function for getting the nsIFile for the maintenance service
 * directory on Windows.
 *
 * @return  The nsIFile for the maintenance service directory.
 * @throws  If called from a platform other than Windows.
 */
function getMaintSvcDir() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  const CSIDL_PROGRAM_FILES = 0x26;
  const CSIDL_PROGRAM_FILESX86 = 0x2a;
  // This will return an empty string on our Win XP build systems.
  let maintSvcDir = getSpecialFolderDir(CSIDL_PROGRAM_FILESX86);
  if (maintSvcDir) {
    maintSvcDir.append("Mozilla Maintenance Service");
    debugDump(
      "using CSIDL_PROGRAM_FILESX86 - maintenance service install " +
        "directory path: " +
        maintSvcDir.path
    );
  }
  if (!maintSvcDir || !maintSvcDir.exists()) {
    maintSvcDir = getSpecialFolderDir(CSIDL_PROGRAM_FILES);
    if (maintSvcDir) {
      maintSvcDir.append("Mozilla Maintenance Service");
      debugDump(
        "using CSIDL_PROGRAM_FILES - maintenance service install " +
          "directory path: " +
          maintSvcDir.path
      );
    }
  }
  if (!maintSvcDir) {
    do_throw("Unable to find the maintenance service install directory");
  }

  return maintSvcDir;
}

/**
 * Reads the current update operation/state in the status file in the secure
 * update log directory.
 *
 * @return The status value.
 */
function readSecureStatusFile() {
  let file = getSecureOutputFile("status");
  if (!file.exists()) {
    debugDump("update status file does not exist, path: " + file.path);
    return STATE_NONE;
  }
  return readFile(file).split("\n")[0];
}

/**
 * Get an nsIFile for a file in the secure update log directory. The file name
 * is always the value of gTestID and the file extension is specified by the
 * aFileExt parameter.
 *
 * @param  aFileExt
 *         The file extension.
 * @return The nsIFile of the secure update file.
 */
function getSecureOutputFile(aFileExt) {
  let file = getMaintSvcDir();
  file.append("UpdateLogs");
  file.append(gTestID + "." + aFileExt);
  return file;
}

/**
 * Get the nsIFile for a Windows special folder determined by the CSIDL
 * passed.
 *
 * @param   aCSIDL
 *          The CSIDL for the Windows special folder.
 * @return  The nsIFile for the Windows special folder.
 * @throws  If called from a platform other than Windows.
 */
function getSpecialFolderDir(aCSIDL) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  let lib = ctypes.open("shell32");
  let SHGetSpecialFolderPath = lib.declare(
    "SHGetSpecialFolderPathW",
    ctypes.winapi_abi,
    ctypes.bool /* bool(return) */,
    ctypes.int32_t /* HWND hwndOwner */,
    ctypes.char16_t.ptr /* LPTSTR lpszPath */,
    ctypes.int32_t /* int csidl */,
    ctypes.bool /* BOOL fCreate */
  );

  let aryPath = ctypes.char16_t.array()(260);
  let rv = SHGetSpecialFolderPath(0, aryPath, aCSIDL, false);
  if (!rv) {
    do_throw(
      "SHGetSpecialFolderPath failed to retrieve " +
        aCSIDL +
        " with Win32 error " +
        ctypes.winLastError
    );
  }
  lib.close();

  let path = aryPath.readString(); // Convert the c-string to js-string
  if (!path) {
    return null;
  }
  debugDump("SHGetSpecialFolderPath returned path: " + path);
  let dir = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  dir.initWithPath(path);
  return dir;
}

ChromeUtils.defineLazyGetter(
  this,
  "gInstallDirPathHash",
  function test_gIDPH() {
    if (AppConstants.platform != "win") {
      do_throw("Windows only function called by a different platform!");
    }

    if (!MOZ_APP_BASENAME) {
      return null;
    }

    let vendor = MOZ_APP_VENDOR ? MOZ_APP_VENDOR : "Mozilla";
    let appDir = getApplyDirFile();

    const REG_PATH =
      "SOFTWARE\\" + vendor + "\\" + MOZ_APP_BASENAME + "\\TaskBarIDs";
    let regKey = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );
    try {
      regKey.open(
        Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
        REG_PATH,
        Ci.nsIWindowsRegKey.ACCESS_ALL
      );
      regKey.writeStringValue(appDir.path, gTestID);
      return gTestID;
    } catch (e) {}

    try {
      regKey.create(
        Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
        REG_PATH,
        Ci.nsIWindowsRegKey.ACCESS_ALL
      );
      regKey.writeStringValue(appDir.path, gTestID);
      return gTestID;
    } catch (e) {
      logTestInfo(
        "failed to create registry value. Registry Path: " +
          REG_PATH +
          ", Value Name: " +
          appDir.path +
          ", Value Data: " +
          gTestID +
          ", Exception " +
          e
      );
      do_throw(
        "Unable to write HKLM or HKCU TaskBarIDs registry value, key path: " +
          REG_PATH
      );
    }
    return null;
  }
);

ChromeUtils.defineLazyGetter(this, "gLocalAppDataDir", function test_gLADD() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  const CSIDL_LOCAL_APPDATA = 0x1c;
  return getSpecialFolderDir(CSIDL_LOCAL_APPDATA);
});

ChromeUtils.defineLazyGetter(this, "gCommonAppDataDir", function test_gCDD() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  const CSIDL_COMMON_APPDATA = 0x0023;
  return getSpecialFolderDir(CSIDL_COMMON_APPDATA);
});

ChromeUtils.defineLazyGetter(this, "gProgFilesDir", function test_gPFD() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  const CSIDL_PROGRAM_FILES = 0x26;
  return getSpecialFolderDir(CSIDL_PROGRAM_FILES);
});

/**
 * Helper function for getting the update root directory used by the tests. This
 * returns the same directory as returned by nsXREDirProvider::GetUpdateRootDir
 * in nsXREDirProvider.cpp so an application will be able to find the update
 * when running a test that launches the application.
 *
 * The aGetOldLocation argument performs the same function that the argument
 * with the same name in nsXREDirProvider::GetUpdateRootDir performs. If true,
 * the old (pre-migration) update directory is returned.
 */
function getMockUpdRootD(aGetOldLocation = false) {
  if (AppConstants.platform == "win") {
    return getMockUpdRootDWin(aGetOldLocation);
  }

  if (AppConstants.platform == "macosx") {
    return getMockUpdRootDMac();
  }

  return getApplyDirFile(DIR_MACOS);
}

/**
 * Helper function for getting the update root directory used by the tests. This
 * returns the same directory as returned by nsXREDirProvider::GetUpdateRootDir
 * in nsXREDirProvider.cpp so an application will be able to find the update
 * when running a test that launches the application.
 *
 * @throws  If called from a platform other than Windows.
 */
function getMockUpdRootDWin(aGetOldLocation) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  let relPathUpdates = "";
  let dataDirectory = gCommonAppDataDir.clone();
  if (aGetOldLocation) {
    relPathUpdates += "Mozilla";
  } else {
    relPathUpdates += "Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38";
  }

  relPathUpdates += "\\" + DIR_UPDATES + "\\" + gInstallDirPathHash;
  let updatesDir = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  updatesDir.initWithPath(dataDirectory.path + "\\" + relPathUpdates);
  return updatesDir;
}

function createWorldWritableAppUpdateDir() {
  // This function is only necessary in Windows
  if (AppConstants.platform == "win") {
    let installDir = Services.dirsvc.get(
      XRE_EXECUTABLE_FILE,
      Ci.nsIFile
    ).parent;
    let exitValue = runTestHelperSync(["create-update-dir", installDir.path]);
    Assert.equal(exitValue, 0, "The helper process exit value should be 0");
  }
}

ChromeUtils.defineLazyGetter(this, "gUpdatesRootDir", function test_gURD() {
  if (AppConstants.platform != "macosx") {
    do_throw("Mac OS X only function called by a different platform!");
  }

  let dir = Services.dirsvc.get("ULibDir", Ci.nsIFile);
  dir.append("Caches");
  if (MOZ_APP_VENDOR || MOZ_APP_BASENAME) {
    dir.append(MOZ_APP_VENDOR ? MOZ_APP_VENDOR : MOZ_APP_BASENAME);
  } else {
    dir.append("Mozilla");
  }
  dir.append(DIR_UPDATES);
  return dir;
});

/**
 * Helper function for getting the update root directory used by the tests. This
 * returns the same directory as returned by nsXREDirProvider::GetUpdateRootDir
 * in nsXREDirProvider.cpp so an application will be able to find the update
 * when running a test that launches the application.
 */
function getMockUpdRootDMac() {
  if (AppConstants.platform != "macosx") {
    do_throw("Mac OS X only function called by a different platform!");
  }

  let exeFile = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  exeFile.initWithPath(gCustomGeneralPaths[XRE_EXECUTABLE_FILE]);
  let appDir = exeFile.parent.parent.parent;
  let appDirPath = appDir.path;
  appDirPath = appDirPath.substr(0, appDirPath.length - 4);

  let pathUpdates = gUpdatesRootDir.path + appDirPath;
  let updatesDir = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  updatesDir.initWithPath(pathUpdates);
  return updatesDir;
}

/**
 * Creates an update in progress lock file in the specified directory on
 * Windows.
 *
 * @param   aDir
 *          The nsIFile for the directory where the lock file should be created.
 * @throws  If called from a platform other than Windows.
 */
function createUpdateInProgressLockFile(aDir) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  let file = aDir.clone();
  file.append(FILE_UPDATE_IN_PROGRESS_LOCK);
  file.create(file.NORMAL_FILE_TYPE, 0o444);
  file.QueryInterface(Ci.nsILocalFileWin);
  file.readOnly = true;
  Assert.ok(file.exists(), MSG_SHOULD_EXIST + getMsgPath(file.path));
  Assert.ok(!file.isWritable(), "the lock file should not be writeable");
}

/**
 * Removes an update in progress lock file in the specified directory on
 * Windows.
 *
 * @param   aDir
 *          The nsIFile for the directory where the lock file is located.
 * @throws  If called from a platform other than Windows.
 */
function removeUpdateInProgressLockFile(aDir) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  let file = aDir.clone();
  file.append(FILE_UPDATE_IN_PROGRESS_LOCK);
  file.QueryInterface(Ci.nsILocalFileWin);
  file.readOnly = false;
  file.remove(false);
  Assert.ok(!file.exists(), MSG_SHOULD_NOT_EXIST + getMsgPath(file.path));
}

function stripQuarantineBitFromPath(aPath) {
  if (AppConstants.platform != "macosx") {
    do_throw("macOS-only function called by a different platform!");
  }

  let args = ["-dr", "com.apple.quarantine", aPath];
  let stripQuarantineBitProcess = Cc[
    "@mozilla.org/process/util;1"
  ].createInstance(Ci.nsIProcess);
  let xattrBin = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  xattrBin.initWithPath("/usr/bin/xattr");
  stripQuarantineBitProcess.init(xattrBin);
  stripQuarantineBitProcess.run(true, args, args.length);
}

function selfSignAppBundle(aPath) {
  if (AppConstants.platform != "macosx") {
    do_throw("macOS-only function called by a different platform!");
  }

  let args = ["--deep", "-fs", "-", aPath];
  let codesignProcess = Cc["@mozilla.org/process/util;1"].createInstance(
    Ci.nsIProcess
  );
  let codesignBin = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  codesignBin.initWithPath("/usr/bin/codesign");
  codesignProcess.init(codesignBin);
  codesignProcess.run(true, args, args.length);
}

/**
 * Copies the test updater to the GRE binary directory and returns the nsIFile
 * for the copied test updater.
 *
 * @return  nsIFIle for the copied test updater.
 */
function copyTestUpdaterToBinDir() {
  let testUpdater = getTestDirFile(FILE_UPDATER_BIN);
  let updater = getGREBinDir();
  updater.append(FILE_UPDATER_BIN);
  if (!updater.exists()) {
    testUpdater.copyToFollowingLinks(updater.parent, FILE_UPDATER_BIN);
  }

  if (AppConstants.platform == "macosx") {
    stripQuarantineBitFromPath(updater.path);
    updater.append("Contents");
    updater.append("MacOS");
    updater.append("org.mozilla.updater");
  }
  return updater;
}

/**
 * Logs the contents of an update log and for maintenance service tests this
 * will log the contents of the latest maintenanceservice.log.
 *
 * @param   aLogLeafName
 *          The leaf name of the update log.
 */
function logUpdateLog(aLogLeafName) {
  let updateLog = getUpdateDirFile(aLogLeafName);
  if (updateLog.exists()) {
    // xpcshell tests won't display the entire contents so log each line.
    let updateLogContents = readFileBytes(updateLog).replace(/\r\n/g, "\n");
    updateLogContents = removeTimeStamps(updateLogContents);
    updateLogContents = replaceLogPaths(updateLogContents);
    let aryLogContents = updateLogContents.split("\n");
    logTestInfo("contents of " + updateLog.path + ":");
    aryLogContents.forEach(function LU_ULC_FE(aLine) {
      logTestInfo(aLine);
    });
  } else {
    logTestInfo("update log doesn't exist, path: " + updateLog.path);
  }

  if (gIsServiceTest) {
    let secureStatus = readSecureStatusFile();
    logTestInfo("secure update status: " + secureStatus);

    updateLog = getSecureOutputFile("log");
    if (updateLog.exists()) {
      // xpcshell tests won't display the entire contents so log each line.
      let updateLogContents = readFileBytes(updateLog).replace(/\r\n/g, "\n");
      updateLogContents = removeTimeStamps(updateLogContents);
      updateLogContents = replaceLogPaths(updateLogContents);
      let aryLogContents = updateLogContents.split("\n");
      logTestInfo("contents of " + updateLog.path + ":");
      aryLogContents.forEach(function LU_SULC_FE(aLine) {
        logTestInfo(aLine);
      });
    } else {
      logTestInfo("secure update log doesn't exist, path: " + updateLog.path);
    }

    let serviceLog = getMaintSvcDir();
    serviceLog.append("logs");
    serviceLog.append("maintenanceservice.log");
    if (serviceLog.exists()) {
      // xpcshell tests won't display the entire contents so log each line.
      let serviceLogContents = readFileBytes(serviceLog).replace(/\r\n/g, "\n");
      serviceLogContents = replaceLogPaths(serviceLogContents);
      let aryLogContents = serviceLogContents.split("\n");
      logTestInfo("contents of " + serviceLog.path + ":");
      aryLogContents.forEach(function LU_MSLC_FE(aLine) {
        logTestInfo(aLine);
      });
    } else {
      logTestInfo(
        "maintenance service log doesn't exist, path: " + serviceLog.path
      );
    }
  }
}

/**
 * Gets the maintenance service log contents.
 */
function readServiceLogFile() {
  let file = getMaintSvcDir();
  file.append("logs");
  file.append("maintenanceservice.log");
  return readFile(file);
}

/**
 * Launches the updater binary to apply an update for updater tests.
 *
 * @param   aExpectedStatus
 *          The expected value of update.status when the update finishes. For
 *          service tests passing STATE_PENDING or STATE_APPLIED will change the
 *          value to STATE_PENDING_SVC and STATE_APPLIED_SVC respectively.
 * @param   aSwitchApp
 *          If true the update should switch the application with an updated
 *          staged application and if false the update should be applied to the
 *          installed application.
 * @param   aExpectedExitValue
 *          The expected exit value from the updater binary for non-service
 *          tests.
 * @param   aCheckSvcLog
 *          Whether the service log should be checked for service tests.
 * @param   aPatchDirPath (optional)
 *          When specified the patch directory path to use for invalid argument
 *          tests otherwise the normal path will be used.
 * @param   aInstallDirPath (optional)
 *          When specified the install directory path to use for invalid
 *          argument tests otherwise the normal path will be used.
 * @param   aApplyToDirPath (optional)
 *          When specified the apply to / working directory path to use for
 *          invalid argument tests otherwise the normal path will be used.
 * @param   aCallbackPath (optional)
 *          When specified the callback path to use for invalid argument tests
 *          otherwise the normal path will be used.
 */
function runUpdate(
  aExpectedStatus,
  aSwitchApp,
  aExpectedExitValue,
  aCheckSvcLog,
  aPatchDirPath,
  aInstallDirPath,
  aApplyToDirPath,
  aCallbackPath
) {
  let isInvalidArgTest =
    !!aPatchDirPath ||
    !!aInstallDirPath ||
    !!aApplyToDirPath ||
    !!aCallbackPath;

  let svcOriginalLog;
  if (gIsServiceTest) {
    copyFileToTestAppDir(FILE_MAINTENANCE_SERVICE_BIN, DIR_MACOS);
    copyFileToTestAppDir(FILE_MAINTENANCE_SERVICE_INSTALLER_BIN, DIR_MACOS);
    if (aCheckSvcLog) {
      svcOriginalLog = readServiceLogFile();
    }
  }

  let pid = 0;
  if (gPIDPersistProcess) {
    pid = gPIDPersistProcess.pid;
    Services.env.set("MOZ_TEST_SHORTER_WAIT_PID", "1");
  }

  if (!gUpdateBin) {
    gUpdateBin = copyTestUpdaterToBinDir();
  }

  Assert.ok(
    gUpdateBin.exists(),
    MSG_SHOULD_EXIST + getMsgPath(gUpdateBin.path)
  );

  let updatesDirPath = aPatchDirPath || getUpdateDirFile(DIR_PATCH).path;
  let installDirPath = aInstallDirPath || getApplyDirFile().path;
  let applyToDirPath = aApplyToDirPath || getApplyDirFile().path;
  let stageDirPath = aApplyToDirPath || getStageDirFile().path;

  let callbackApp = getApplyDirFile(DIR_MACOS + gCallbackApp);
  Assert.ok(
    callbackApp.exists(),
    MSG_SHOULD_EXIST + ", path: " + callbackApp.path
  );
  callbackApp.permissions = PERMS_DIRECTORY;

  setAppBundleModTime();

  // The version 3 argument format looks like
  // updater 3 patch-dir install-dir apply-to-dir which-invocation [wait-pid [callback-working-dir callback-path args...]]
  let args = [
    "3",
    updatesDirPath,
    installDirPath,
    aSwitchApp ? stageDirPath : applyToDirPath,
    "first",
    aSwitchApp ? pid + "/replace" : pid,
  ];

  let launchBin = gIsServiceTest && isInvalidArgTest ? callbackApp : gUpdateBin;

  if (!isInvalidArgTest) {
    args = args.concat([callbackApp.parent.path, callbackApp.path]);
    args = args.concat(gCallbackArgs);
  } else if (gIsServiceTest) {
    // We are jumping straight to the second invocation in this case
    args[4] = "second";
    args = ["launch-service", gUpdateBin.path].concat(args);
  } else if (aCallbackPath) {
    args = args.concat([callbackApp.parent.path, aCallbackPath]);
  }

  debugDump("launching the program: " + launchBin.path + " " + args.join(" "));

  if (aSwitchApp && !isInvalidArgTest) {
    // We want to set the env vars again
    gShouldResetEnv = undefined;
  }

  setEnvironment();

  let process = Cc["@mozilla.org/process/util;1"].createInstance(Ci.nsIProcess);
  process.init(launchBin);
  process.run(true, args, args.length);

  resetEnvironment();

  if (gPIDPersistProcess) {
    Services.env.set("MOZ_TEST_SHORTER_WAIT_PID", "");
  }

  let status = readStatusFile();
  if (
    (!gIsServiceTest && process.exitValue != aExpectedExitValue) ||
    (status != aExpectedStatus && !gIsServiceTest && !isInvalidArgTest)
  ) {
    if (process.exitValue != aExpectedExitValue) {
      logTestInfo(
        "updater exited with unexpected value! Got: " +
          process.exitValue +
          ", Expected: " +
          aExpectedExitValue
      );
    }
    if (status != aExpectedStatus) {
      logTestInfo(
        "update status is not the expected status! Got: " +
          status +
          ", Expected: " +
          aExpectedStatus
      );
    }
    logUpdateLog(FILE_LAST_UPDATE_LOG);
  }

  if (gIsServiceTest && isInvalidArgTest) {
    let secureStatus = readSecureStatusFile();
    if (secureStatus != STATE_NONE) {
      status = secureStatus;
    }
  }

  if (!gIsServiceTest) {
    Assert.equal(
      process.exitValue,
      aExpectedExitValue,
      "the process exit value" + MSG_SHOULD_EQUAL
    );
  }

  if (status != aExpectedStatus) {
    logUpdateLog(FILE_UPDATE_LOG);
  }
  Assert.equal(status, aExpectedStatus, "the update status" + MSG_SHOULD_EQUAL);

  if (gIsServiceTest && aCheckSvcLog) {
    let contents = readServiceLogFile();
    Assert.notEqual(
      contents,
      svcOriginalLog,
      "the contents of the maintenanceservice.log should not " +
        "be the same as the original contents"
    );
    if (gEnvForceServiceFallback) {
      // If we are forcing the service to fail and fall back to update without
      // the service, the service log should reflect that we failed in that way.
      Assert.ok(
        contents.includes(LOG_SVC_UNSUCCESSFUL_LAUNCH),
        "the contents of the maintenanceservice.log should " +
          "contain the unsuccessful launch string"
      );
    } else if (!isInvalidArgTest) {
      Assert.notEqual(
        contents.indexOf(LOG_SVC_SUCCESSFUL_LAUNCH),
        -1,
        "the contents of the maintenanceservice.log should " +
          "contain the successful launch string"
      );
    }
  }
}

/**
 * Launches the helper binary synchronously with the specified arguments for
 * updater tests.
 *
 * @param   aArgs
 *          The arguments to pass to the helper binary.
 * @return  the process exit value returned by the helper binary.
 */
function runTestHelperSync(aArgs) {
  let helperBin = getTestDirFile(FILE_HELPER_BIN);
  let process = Cc["@mozilla.org/process/util;1"].createInstance(Ci.nsIProcess);
  process.init(helperBin);
  debugDump("Running " + helperBin.path + " " + aArgs.join(" "));
  process.run(true, aArgs, aArgs.length);
  return process.exitValue;
}

/**
 * Creates a symlink for updater tests.
 */
function createSymlink() {
  let args = [
    "setup-symlink",
    "moz-foo",
    "moz-bar",
    "target",
    getApplyDirFile().path + "/" + DIR_RESOURCES + "link",
  ];
  let exitValue = runTestHelperSync(args);
  Assert.equal(exitValue, 0, "the helper process exit value should be 0");
  let file = getApplyDirFile(DIR_RESOURCES + "link");
  Assert.ok(file.exists(), MSG_SHOULD_EXIST + ", path: " + file.path);
  file.permissions = 0o666;
  args = [
    "setup-symlink",
    "moz-foo2",
    "moz-bar2",
    "target2",
    getApplyDirFile().path + "/" + DIR_RESOURCES + "link2",
    "change-perm",
  ];
  exitValue = runTestHelperSync(args);
  Assert.equal(exitValue, 0, "the helper process exit value should be 0");
}

/**
 * Removes a symlink for updater tests.
 */
function removeSymlink() {
  let args = [
    "remove-symlink",
    "moz-foo",
    "moz-bar",
    "target",
    getApplyDirFile().path + "/" + DIR_RESOURCES + "link",
  ];
  let exitValue = runTestHelperSync(args);
  Assert.equal(exitValue, 0, "the helper process exit value should be 0");
  args = [
    "remove-symlink",
    "moz-foo2",
    "moz-bar2",
    "target2",
    getApplyDirFile().path + "/" + DIR_RESOURCES + "link2",
  ];
  exitValue = runTestHelperSync(args);
  Assert.equal(exitValue, 0, "the helper process exit value should be 0");
}

/**
 * Checks a symlink for updater tests.
 */
function checkSymlink() {
  let args = [
    "check-symlink",
    getApplyDirFile().path + "/" + DIR_RESOURCES + "link",
  ];
  let exitValue = runTestHelperSync(args);
  Assert.equal(exitValue, 0, "the helper process exit value should be 0");
}

/**
 * Sets the active update and related information for updater tests.
 */
async function setupActiveUpdate() {
  // The update system being initialized at an unexpected time could cause
  // unexpected effects in the reload process. Make sure that initialization
  // has already run first.
  await gAUS.init();

  let pendingState = gIsServiceTest ? STATE_PENDING_SVC : STATE_PENDING;
  let patchProps = { state: pendingState };
  let patches = getLocalPatchString(patchProps);
  let updates = getLocalUpdateString({}, patches);
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);
  writeVersionFile(DEFAULT_UPDATE_VERSION);
  writeStatusFile(pendingState);
  reloadUpdateManagerData();
  Assert.ok(
    !!(await gUpdateManager.getReadyUpdate()),
    "the ready update should be defined"
  );
}

/**
 * Stages an update using nsIUpdateProcessor:processUpdate for updater tests.
 *
 * @param   aStateAfterStage
 *          The expected update state after the update has been staged.
 * @param   aCheckSvcLog
 *          Whether the service log should be checked for service tests.
 * @param   aUpdateRemoved (optional)
 *          Whether the update is removed after staging. This can happen when
 *          a staging failure occurs.
 */
async function stageUpdate(
  aStateAfterStage,
  aCheckSvcLog,
  aUpdateRemoved = false
) {
  debugDump("start - attempting to stage update");

  let svcLogOriginalContents;
  if (gIsServiceTest && aCheckSvcLog) {
    svcLogOriginalContents = readServiceLogFile();
  }

  setAppBundleModTime();
  setEnvironment();
  try {
    // Stage the update.
    Cc["@mozilla.org/updates/update-processor;1"]
      .createInstance(Ci.nsIUpdateProcessor)
      .processUpdate();
  } catch (e) {
    Assert.ok(
      false,
      "error thrown while calling processUpdate, Exception: " + e
    );
  }
  await waitForEvent("update-staged", aStateAfterStage);
  resetEnvironment();

  if (AppConstants.platform == "win") {
    if (gIsServiceTest) {
      waitForServiceStop(false);
    } else {
      let updater = getApplyDirFile(FILE_UPDATER_BIN);
      await TestUtils.waitForCondition(
        () => !isFileInUse(updater),
        "Waiting for the file tp not be in use, Path: " + updater.path
      );
    }
  }

  if (!aUpdateRemoved) {
    Assert.equal(
      readStatusState(),
      aStateAfterStage,
      "the status file state" + MSG_SHOULD_EQUAL
    );

    Assert.equal(
      (await gUpdateManager.getReadyUpdate()).state,
      aStateAfterStage,
      "the update state" + MSG_SHOULD_EQUAL
    );
  }

  let log = getUpdateDirFile(FILE_LAST_UPDATE_LOG);
  Assert.ok(log.exists(), MSG_SHOULD_EXIST + getMsgPath(log.path));

  log = getUpdateDirFile(FILE_UPDATE_LOG);
  Assert.ok(!log.exists(), MSG_SHOULD_NOT_EXIST + getMsgPath(log.path));

  log = getUpdateDirFile(FILE_BACKUP_UPDATE_LOG);
  Assert.ok(!log.exists(), MSG_SHOULD_NOT_EXIST + getMsgPath(log.path));

  let stageDir = getStageDirFile();
  if (
    aStateAfterStage == STATE_APPLIED ||
    aStateAfterStage == STATE_APPLIED_SVC
  ) {
    Assert.ok(stageDir.exists(), MSG_SHOULD_EXIST + getMsgPath(stageDir.path));
  } else {
    Assert.ok(
      !stageDir.exists(),
      MSG_SHOULD_NOT_EXIST + getMsgPath(stageDir.path)
    );
  }

  if (gIsServiceTest && aCheckSvcLog) {
    let contents = readServiceLogFile();
    Assert.notEqual(
      contents,
      svcLogOriginalContents,
      "the contents of the maintenanceservice.log should not " +
        "be the same as the original contents"
    );
    Assert.notEqual(
      contents.indexOf(LOG_SVC_SUCCESSFUL_LAUNCH),
      -1,
      "the contents of the maintenanceservice.log should " +
        "contain the successful launch string"
    );
  }

  debugDump("finish - attempting to stage update");
}

/**
 * Helper function to check whether the maintenance service updater tests should
 * run. See bug 711660 for more details.
 *
 * @return true if the test should run and false if it shouldn't.
 * @throws  If called from a platform other than Windows.
 */
function shouldRunServiceTest() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  let binDir = getGREBinDir();
  let updaterBin = binDir.clone();
  updaterBin.append(FILE_UPDATER_BIN);
  Assert.ok(
    updaterBin.exists(),
    MSG_SHOULD_EXIST + ", leafName: " + updaterBin.leafName
  );

  let updaterBinPath = updaterBin.path;
  if (/ /.test(updaterBinPath)) {
    updaterBinPath = '"' + updaterBinPath + '"';
  }

  let isBinSigned = isBinarySigned(updaterBinPath);

  const REG_PATH =
    "SOFTWARE\\Mozilla\\MaintenanceService\\" +
    "3932ecacee736d366d6436db0f55bce4";
  let key = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
    Ci.nsIWindowsRegKey
  );
  try {
    key.open(
      Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
      REG_PATH,
      Ci.nsIWindowsRegKey.ACCESS_READ | key.WOW64_64
    );
  } catch (e) {
    // The build system could sign the files and not have the test registry key
    // in which case we should fail the test if the updater binary is signed so
    // the build system can be fixed by adding the registry key.
    if (IS_AUTHENTICODE_CHECK_ENABLED) {
      Assert.ok(
        !isBinSigned,
        "the updater.exe binary should not be signed when the test " +
          "registry key doesn't exist (if it is, build system " +
          "configuration bug?)"
      );
    }

    logTestInfo(
      "this test can only run on the buildbot build system at this time"
    );
    return false;
  }

  // Check to make sure the service is installed
  let args = ["wait-for-service-stop", "MozillaMaintenance", "10"];
  let exitValue = runTestHelperSync(args);
  Assert.notEqual(
    exitValue,
    0xee,
    "the maintenance service should be " +
      "installed (if not, build system configuration bug?)"
  );

  if (IS_AUTHENTICODE_CHECK_ENABLED) {
    // The test registry key exists and IS_AUTHENTICODE_CHECK_ENABLED is true
    // so the binaries should be signed. To run the test locally
    // DISABLE_UPDATER_AUTHENTICODE_CHECK can be defined.
    Assert.ok(
      isBinSigned,
      "the updater.exe binary should be signed (if not, build system " +
        "configuration bug?)"
    );
  }

  // In case the machine is running an old maintenance service or if it
  // is not installed, and permissions exist to install it. Then install
  // the newer bin that we have since all of the other checks passed.
  return attemptServiceInstall();
}

/**
 * Helper function to check whether the a binary is signed.
 *
 * @param   aBinPath
 *          The path to the file to check if it is signed.
 * @return  true if the file is signed and false if it isn't.
 */
function isBinarySigned(aBinPath) {
  let args = ["check-signature", aBinPath];
  let exitValue = runTestHelperSync(args);
  if (exitValue != 0) {
    logTestInfo(
      "binary is not signed. " +
        FILE_HELPER_BIN +
        " returned " +
        exitValue +
        " for file " +
        aBinPath
    );
    return false;
  }
  return true;
}

/**
 * Helper function for setting up the application files required to launch the
 * application for the updater tests by either copying or creating symlinks to
 * the files.
 *
 * @param options.requiresOmnijar when true, copy or symlink omnijars as well.
 * This may be required to launch the updated application and have non-trivial
 * functionality available.
 */
function setupAppFiles({ requiresOmnijar = false } = {}) {
  debugDump(
    "start - copying or creating symlinks to application files " +
      "for the test"
  );

  let destDir = getApplyDirFile();
  if (!destDir.exists()) {
    try {
      destDir.create(Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
    } catch (e) {
      logTestInfo(
        "unable to create directory! Path: " +
          destDir.path +
          ", Exception: " +
          e
      );
      do_throw(e);
    }
  }

  // Required files for the application or the test that aren't listed in the
  // dependentlibs.list file.
  let appFiles = [
    { relPath: FILE_APP_BIN, inDir: DIR_MACOS },
    { relPath: FILE_APPLICATION_INI, inDir: DIR_RESOURCES },
    { relPath: "dependentlibs.list", inDir: DIR_RESOURCES },
  ];

  if (requiresOmnijar) {
    appFiles.push({ relPath: AppConstants.OMNIJAR_NAME, inDir: DIR_RESOURCES });

    if (AppConstants.MOZ_BUILD_APP == "browser") {
      // Only Firefox uses an app-specific omnijar.
      appFiles.push({
        relPath: "browser/" + AppConstants.OMNIJAR_NAME,
        inDir: DIR_RESOURCES,
      });
    }
  }

  // On Linux the updater.png must also be copied and libsoftokn3.so must be
  // symlinked or copied.
  if (AppConstants.platform == "linux") {
    appFiles.push(
      { relPath: "icons/updater.png", inDir: DIR_RESOURCES },
      { relPath: "libsoftokn3.so", inDir: DIR_RESOURCES }
    );
  }

  // Read the dependent libs file leafnames from the dependentlibs.list file
  // into the array.
  let deplibsFile = gGREDirOrig.clone();
  deplibsFile.append("dependentlibs.list");
  let fis = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fis.init(deplibsFile, 0x01, 0o444, Ci.nsIFileInputStream.CLOSE_ON_EOF);
  fis.QueryInterface(Ci.nsILineInputStream);

  let hasMore;
  let line = {};
  do {
    hasMore = fis.readLine(line);
    appFiles.push({ relPath: line.value, inDir: DIR_MACOS });
  } while (hasMore);

  fis.close();

  appFiles.forEach(function CMAF_FLN_FE(aAppFile) {
    copyFileToTestAppDir(aAppFile.relPath, aAppFile.inDir);
  });

  copyTestUpdaterToBinDir();

  debugDump(
    "finish - copying or creating symlinks to application files " +
      "for the test"
  );
}

/**
 * Copies the specified files from the dist/bin directory into the test's
 * application directory.
 *
 * @param   aFileRelPath
 *          The relative path to the source and the destination of the file to
 *          copy.
 * @param   aDir
 *          The relative subdirectory within the .app bundle on macOS. This is
 *          ignored on all other platforms.
 */
function copyFileToTestAppDir(aFileRelPath, aDir) {
  let srcFile;
  let destFile;

  // gGREDirOrig and gGREBinDirOrig must always be cloned when changing its
  // properties
  if (AppConstants.platform == "macosx") {
    switch (aDir) {
      case DIR_RESOURCES:
        srcFile = gGREDirOrig.clone();
        destFile = getGREDir();
        break;
      case DIR_MACOS:
        srcFile = gGREBinDirOrig.clone();
        destFile = getGREBinDir();
        break;
      case DIR_CONTENTS:
        srcFile = gGREBinDirOrig.parent.clone();
        destFile = getGREBinDir().parent;
        break;
      default:
        debugDump("invalid path given. Path: " + aDir);
        break;
    }
  } else {
    srcFile = gGREDirOrig.clone();
    destFile = getGREDir();
  }

  let fileRelPath = aFileRelPath;
  let pathParts = fileRelPath.split("/");
  for (let i = 0; i < pathParts.length; i++) {
    if (pathParts[i]) {
      srcFile.append(pathParts[i]);
      destFile.append(pathParts[i]);
    }
  }

  if (AppConstants.platform == "macosx" && !srcFile.exists()) {
    debugDump(
      "unable to copy file since it doesn't exist! Checking if " +
        fileRelPath +
        ".app exists. Path: " +
        srcFile.path
    );
    for (let i = 0; i < pathParts.length; i++) {
      if (pathParts[i]) {
        srcFile.append(
          pathParts[i] + (pathParts.length - 1 == i ? ".app" : "")
        );
        destFile.append(
          pathParts[i] + (pathParts.length - 1 == i ? ".app" : "")
        );
      }
    }
    fileRelPath = fileRelPath + ".app";
  }
  Assert.ok(
    srcFile.exists(),
    MSG_SHOULD_EXIST + ", leafName: " + srcFile.leafName
  );

  // Symlink libraries. Note that the XUL library on Mac OS X doesn't have a
  // file extension and shouldSymlink will always be false on Windows.
  let shouldSymlink =
    pathParts[pathParts.length - 1] == "XUL" ||
    fileRelPath.substr(fileRelPath.length - 3) == ".so" ||
    fileRelPath.substr(fileRelPath.length - 6) == ".dylib";
  if (!shouldSymlink) {
    if (!destFile.exists()) {
      try {
        srcFile.copyToFollowingLinks(destFile.parent, destFile.leafName);
      } catch (e) {
        // Just in case it is partially copied
        if (destFile.exists()) {
          try {
            destFile.remove(true);
          } catch (ex) {
            logTestInfo(
              "unable to remove file that failed to copy! Path: " +
                destFile.path +
                ", Exception: " +
                ex
            );
          }
        }
        do_throw(
          "Unable to copy file! Path: " + srcFile.path + ", Exception: " + e
        );
      }
    }
  } else {
    try {
      if (destFile.exists()) {
        destFile.remove(false);
      }
      let ln = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
      ln.initWithPath("/bin/ln");
      let process = Cc["@mozilla.org/process/util;1"].createInstance(
        Ci.nsIProcess
      );
      process.init(ln);
      let args = ["-s", srcFile.path, destFile.path];
      process.run(true, args, args.length);
      Assert.ok(
        destFile.isSymlink(),
        destFile.leafName + " should be a symlink"
      );
    } catch (e) {
      do_throw(
        "Unable to create symlink for file! Path: " +
          srcFile.path +
          ", Exception: " +
          e
      );
    }
  }
}

/**
 * Attempts to upgrade the maintenance service if permissions are allowed.
 * This is useful for XP where we have permission to upgrade in case an
 * older service installer exists.  Also if the user manually installed into
 * a unprivileged location.
 *
 * @return true if the installed service is from this build. If the installed
 *         service is not from this build the test will fail instead of
 *         returning false.
 * @throws  If called from a platform other than Windows.
 */
function attemptServiceInstall() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  let maintSvcDir = getMaintSvcDir();
  Assert.ok(
    maintSvcDir.exists(),
    MSG_SHOULD_EXIST + ", leafName: " + maintSvcDir.leafName
  );
  let oldMaintSvcBin = maintSvcDir.clone();
  oldMaintSvcBin.append(FILE_MAINTENANCE_SERVICE_BIN);
  Assert.ok(
    oldMaintSvcBin.exists(),
    MSG_SHOULD_EXIST + ", leafName: " + oldMaintSvcBin.leafName
  );
  let buildMaintSvcBin = getGREBinDir();
  buildMaintSvcBin.append(FILE_MAINTENANCE_SERVICE_BIN);
  if (readFileBytes(oldMaintSvcBin) == readFileBytes(buildMaintSvcBin)) {
    debugDump(
      "installed maintenance service binary is the same as the " +
        "build's maintenance service binary"
    );
    return true;
  }
  let backupMaintSvcBin = maintSvcDir.clone();
  backupMaintSvcBin.append(FILE_MAINTENANCE_SERVICE_BIN + ".backup");
  try {
    if (backupMaintSvcBin.exists()) {
      backupMaintSvcBin.remove(false);
    }
    oldMaintSvcBin.moveTo(
      maintSvcDir,
      FILE_MAINTENANCE_SERVICE_BIN + ".backup"
    );
    buildMaintSvcBin.copyTo(maintSvcDir, FILE_MAINTENANCE_SERVICE_BIN);
    backupMaintSvcBin.remove(false);
  } catch (e) {
    // Restore the original file in case the moveTo was successful.
    if (backupMaintSvcBin.exists()) {
      oldMaintSvcBin = maintSvcDir.clone();
      oldMaintSvcBin.append(FILE_MAINTENANCE_SERVICE_BIN);
      if (!oldMaintSvcBin.exists()) {
        backupMaintSvcBin.moveTo(maintSvcDir, FILE_MAINTENANCE_SERVICE_BIN);
      }
    }
    Assert.ok(
      false,
      "should be able copy the test maintenance service to " +
        "the maintenance service directory (if not, build system " +
        "configuration bug?), path: " +
        maintSvcDir.path
    );
  }

  return true;
}

/**
 * Waits for the applications that are launched by the maintenance service to
 * stop.
 *
 * @throws  If called from a platform other than Windows.
 */
function waitServiceApps() {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  // maintenanceservice_installer.exe is started async during updates.
  waitForApplicationStop("maintenanceservice_installer.exe");
  // maintenanceservice_tmp.exe is started async from the service installer.
  waitForApplicationStop("maintenanceservice_tmp.exe");
  // In case the SCM thinks the service is stopped, but process still exists.
  waitForApplicationStop("maintenanceservice.exe");
}

/**
 * Waits for the maintenance service to stop.
 *
 * @throws  If called from a platform other than Windows.
 */
function waitForServiceStop(aFailTest) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  waitServiceApps();
  debugDump("waiting for the maintenance service to stop if necessary");
  // Use the helper bin to ensure the service is stopped. If not stopped, then
  // wait for the service to stop (at most 120 seconds).
  let args = ["wait-for-service-stop", "MozillaMaintenance", "120"];
  let exitValue = runTestHelperSync(args);
  Assert.notEqual(exitValue, 0xee, "the maintenance service should exist");
  if (exitValue != 0) {
    if (aFailTest) {
      Assert.ok(
        false,
        "the maintenance service should stop, process exit " +
          "value: " +
          exitValue
      );
    }
    logTestInfo(
      "maintenance service did not stop which may cause test " +
        "failures later, process exit value: " +
        exitValue
    );
  } else {
    debugDump("service stopped");
  }
  waitServiceApps();
}

/**
 * Waits for the specified application to stop.
 *
 * @param   aApplication
 *          The application binary name to wait until it has stopped.
 * @throws  If called from a platform other than Windows.
 */
function waitForApplicationStop(aApplication) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  debugDump("waiting for " + aApplication + " to stop if necessary");
  // Use the helper bin to ensure the application is stopped. If not stopped,
  // then wait for it to stop (at most 120 seconds).
  let args = ["wait-for-application-exit", aApplication, "120"];
  let exitValue = runTestHelperSync(args);
  Assert.equal(
    exitValue,
    0,
    "the process should have stopped, process name: " + aApplication
  );
}

/**
 * Waits for the application with the specified pid to stop.
 *
 * @param   pid
 *          The application identifier to wait on.
 * @param   timeout
 *          The amount of time to wait, if the pid doesn't exit.
 */
function waitForPidStop(pid, timeout = 120) {
  debugDump("waiting for pid " + pid + " to stop if necessary");
  // Use the helper bin to ensure the application is stopped. If not stopped,
  // then wait for it to stop (at most 120 seconds).
  let args = ["wait-for-pid-exit", pid, timeout.toString()];
  let exitValue = runTestHelperSync(args);
  Assert.equal(
    exitValue,
    0,
    "the process should have stopped, process id: " + pid
  );
}

/**
 * Gets the platform specific shell binary that is launched using nsIProcess and
 * in turn launches a binary used for the test (e.g. application, updater,
 * etc.). A shell is used so debug console output can be redirected to a file so
 * it doesn't end up in the test log.
 *
 * @return nsIFile for the shell binary to launch using nsIProcess.
 */
function getLaunchBin() {
  let launchBin;
  if (AppConstants.platform == "win") {
    launchBin = Services.dirsvc.get("WinD", Ci.nsIFile);
    launchBin.append("System32");
    launchBin.append("cmd.exe");
  } else {
    launchBin = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    launchBin.initWithPath("/bin/sh");
  }
  Assert.ok(launchBin.exists(), MSG_SHOULD_EXIST + getMsgPath(launchBin.path));

  return launchBin;
}

/**
 * Locks a Windows directory.
 *
 * @param   aDirPath
 *          The test file object that describes the file to make in use.
 * @throws  If called from a platform other than Windows.
 */
function lockDirectory(aDirPath) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  debugDump("start - locking installation directory");
  const LPCWSTR = ctypes.char16_t.ptr;
  const DWORD = ctypes.uint32_t;
  const LPVOID = ctypes.voidptr_t;
  const GENERIC_READ = 0x80000000;
  const FILE_SHARE_READ = 1;
  const FILE_SHARE_WRITE = 2;
  const OPEN_EXISTING = 3;
  const FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
  const INVALID_HANDLE_VALUE = LPVOID(0xffffffff);
  let kernel32 = ctypes.open("kernel32");
  let CreateFile = kernel32.declare(
    "CreateFileW",
    ctypes.winapi_abi,
    LPVOID,
    LPCWSTR,
    DWORD,
    DWORD,
    LPVOID,
    DWORD,
    DWORD,
    LPVOID
  );
  gHandle = CreateFile(
    aDirPath,
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    LPVOID(0),
    OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS,
    LPVOID(0)
  );
  Assert.notEqual(
    gHandle.toString(),
    INVALID_HANDLE_VALUE.toString(),
    "the handle should not equal INVALID_HANDLE_VALUE"
  );
  kernel32.close();
  debugDump("finish - locking installation directory");
}

/**
 * Launches the test helper binary to make it in use for updater tests.
 *
 * @param   aRelPath
 *          The relative path in the apply to directory for the helper binary.
 * @param   aCopyTestHelper
 *          Whether to copy the test helper binary to the relative path in the
 *          apply to directory.
 */
async function runHelperFileInUse(aRelPath, aCopyTestHelper) {
  debugDump("aRelPath: " + aRelPath);
  // Launch an existing file so it is in use during the update.
  let helperBin = getTestDirFile(FILE_HELPER_BIN);
  let fileInUseBin = getApplyDirFile(aRelPath);
  if (aCopyTestHelper) {
    if (fileInUseBin.exists()) {
      fileInUseBin.remove(false);
    }
    helperBin.copyTo(fileInUseBin.parent, fileInUseBin.leafName);
  }
  fileInUseBin.permissions = PERMS_DIRECTORY;
  let args = [
    getApplyDirPath() + DIR_RESOURCES,
    "input",
    "output",
    "-s",
    HELPER_SLEEP_TIMEOUT,
  ];
  let fileInUseProcess = Cc["@mozilla.org/process/util;1"].createInstance(
    Ci.nsIProcess
  );
  fileInUseProcess.init(fileInUseBin);
  fileInUseProcess.run(false, args, args.length);

  await waitForHelperSleep();
}

/**
 * Launches the test helper binary to provide a pid that is in use for updater
 * tests.
 *
 * @param   aRelPath
 *          The relative path in the apply to directory for the helper binary.
 * @param   aCopyTestHelper
 *          Whether to copy the test helper binary to the relative path in the
 *          apply to directory.
 */
async function runHelperPIDPersists(aRelPath, aCopyTestHelper) {
  debugDump("aRelPath: " + aRelPath);
  // Launch an existing file so it is in use during the update.
  let helperBin = getTestDirFile(FILE_HELPER_BIN);
  let pidPersistsBin = getApplyDirFile(aRelPath);
  if (aCopyTestHelper) {
    if (pidPersistsBin.exists()) {
      pidPersistsBin.remove(false);
    }
    helperBin.copyTo(pidPersistsBin.parent, pidPersistsBin.leafName);
  }
  pidPersistsBin.permissions = PERMS_DIRECTORY;
  let args = [
    getApplyDirPath() + DIR_RESOURCES,
    "input",
    "output",
    "-s",
    HELPER_SLEEP_TIMEOUT,
  ];
  gPIDPersistProcess = Cc["@mozilla.org/process/util;1"].createInstance(
    Ci.nsIProcess
  );
  gPIDPersistProcess.init(pidPersistsBin);
  gPIDPersistProcess.run(false, args, args.length);

  await waitForHelperSleep();
  await TestUtils.waitForCondition(
    () => !!gPIDPersistProcess.pid,
    "Waiting for the process pid"
  );
}

/**
 * Launches the test helper binary and locks a file specified on the command
 * line for updater tests.
 *
 * @param   aTestFile
 *          The test file object that describes the file to lock.
 */
async function runHelperLockFile(aTestFile) {
  // Exclusively lock an existing file so it is in use during the update.
  let helperBin = getTestDirFile(FILE_HELPER_BIN);
  let helperDestDir = getApplyDirFile(DIR_RESOURCES);
  helperBin.copyTo(helperDestDir, FILE_HELPER_BIN);
  helperBin = getApplyDirFile(DIR_RESOURCES + FILE_HELPER_BIN);
  // Strip off the first two directories so the path has to be from the helper's
  // working directory.
  let lockFileRelPath = aTestFile.relPathDir.split("/");
  if (AppConstants.platform == "macosx") {
    lockFileRelPath = lockFileRelPath.slice(2);
  }
  lockFileRelPath = lockFileRelPath.join("/") + "/" + aTestFile.fileName;
  let args = [
    getApplyDirPath() + DIR_RESOURCES,
    "input",
    "output",
    "-s",
    HELPER_SLEEP_TIMEOUT,
    lockFileRelPath,
  ];
  let helperProcess = Cc["@mozilla.org/process/util;1"].createInstance(
    Ci.nsIProcess
  );
  helperProcess.init(helperBin);
  helperProcess.run(false, args, args.length);

  await waitForHelperSleep();
}

/**
 * Helper function that waits until the helper has completed its operations.
 */
async function waitForHelperSleep() {
  // Give the lock file process time to lock the file before updating otherwise
  // this test can fail intermittently on Windows debug builds.
  let file = getApplyDirFile(DIR_RESOURCES + "output");
  await TestUtils.waitForCondition(
    () => file.exists(),
    "Waiting for file to exist, path: " + file.path
  );

  let expectedContents = "sleeping\n";
  await TestUtils.waitForCondition(
    () => readFile(file) == expectedContents,
    "Waiting for expected file contents: " + expectedContents
  );

  await TestUtils.waitForCondition(() => {
    try {
      file.remove(false);
    } catch (e) {
      debugDump(
        "failed to remove file. Path: " + file.path + ", Exception: " + e
      );
    }
    return !file.exists();
  }, "Waiting for file to be removed, Path: " + file.path);
}

/**
 * Helper function to tell the helper to finish and exit its sleep state.
 */
async function waitForHelperExit() {
  let file = getApplyDirFile(DIR_RESOURCES + "input");
  writeFile(file, "finish\n");

  // Give the lock file process time to lock the file before updating otherwise
  // this test can fail intermittently on Windows debug builds.
  file = getApplyDirFile(DIR_RESOURCES + "output");
  await TestUtils.waitForCondition(
    () => file.exists(),
    "Waiting for file to exist, Path: " + file.path
  );

  let expectedContents = "finished\n";
  await TestUtils.waitForCondition(
    () => readFile(file) == expectedContents,
    "Waiting for expected file contents: " + expectedContents
  );

  // Give the lock file process time to unlock the file before deleting the
  // input and output files.
  await TestUtils.waitForCondition(() => {
    try {
      file.remove(false);
    } catch (e) {
      debugDump(
        "failed to remove file. Path: " + file.path + ", Exception: " + e
      );
    }
    return !file.exists();
  }, "Waiting for file to be removed, Path: " + file.path);

  file = getApplyDirFile(DIR_RESOURCES + "input");
  await TestUtils.waitForCondition(() => {
    try {
      file.remove(false);
    } catch (e) {
      debugDump(
        "failed to remove file. Path: " + file.path + ", Exception: " + e
      );
    }
    return !file.exists();
  }, "Waiting for file to be removed, Path: " + file.path);
}

/**
 * Helper function for updater binary tests that creates the files and
 * directories used by the test.
 *
 * @param   aMarFile
 *          The mar file for the update test.
 * @param   aPostUpdateAsync
 *          When null the updater.ini is not created otherwise this parameter
 *          is passed to createUpdaterINI.
 * @param   aPostUpdateExeRelPathPrefix
 *          When aPostUpdateAsync null this value is ignored otherwise it is
 *          passed to createUpdaterINI.
 * @param   aSetupActiveUpdate
 *          Whether to setup the active update.
 *
 * @param   options.requiresOmnijar
 *          When true, copy or symlink omnijars as well.  This may be required
 *          to launch the updated application and have non-trivial functionality
 *          available.
 * @param   options.asyncExeArg
 *          When `aPostUpdateAsync`, the (single) post-argument to invoke the
 *          post-update process with.  Default: "post-update-async".
 */
async function setupUpdaterTest(
  aMarFile,
  aPostUpdateAsync,
  aPostUpdateExeRelPathPrefix = "",
  aSetupActiveUpdate = true,
  { requiresOmnijar = false, asyncExeArg = "post-update-async" } = {}
) {
  debugDump("start - updater test setup");
  // Make sure that update has already been initialized. If post update
  // processing unexpectedly runs between this setup and when we use these
  // files, it may clean them up before we get the chance to use them.
  await gAUS.init();

  let updatesPatchDir = getUpdateDirFile(DIR_PATCH);
  if (!updatesPatchDir.exists()) {
    updatesPatchDir.create(Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
  }
  // Copy the mar that will be applied
  let mar = getTestDirFile(aMarFile);
  mar.copyToFollowingLinks(updatesPatchDir, FILE_UPDATE_MAR);

  let helperApp = getTestDirFile(FILE_HELPER_APP);
  let helperBin = getTestDirFile(FILE_HELPER_BIN);
  helperApp.permissions = PERMS_DIRECTORY;
  helperBin.permissions = PERMS_DIRECTORY;
  let afterApplyBinDir = getApplyDirFile(DIR_MACOS);

  helperBin.copyToFollowingLinks(afterApplyBinDir, gPostUpdateBinFile);
  helperApp.copyToFollowingLinks(afterApplyBinDir, gCallbackApp);

  // On macOS, some test files (like the Update Settings file) may be within the
  // updater app bundle, so make sure it is in place now in case we want to
  // manipulate it.
  if (!gUpdateBin) {
    gUpdateBin = copyTestUpdaterToBinDir();
  }

  let filesWithCompareContents = [];
  gTestFiles.forEach(function SUT_TF_FE(aTestFile) {
    debugDump("start - setup test file: " + aTestFile.fileName);
    if (aTestFile.originalFile || aTestFile.originalContents) {
      let testDir = getApplyDirFile(aTestFile.relPathDir);
      // Somehow these create calls are failing with FILE_ALREADY_EXISTS even
      // after checking .exists() first, so we just catch the exception.
      try {
        testDir.create(Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
      } catch (e) {
        if (e.result != Cr.NS_ERROR_FILE_ALREADY_EXISTS) {
          throw e;
        }
      }

      let testFile;
      if (aTestFile.originalFile) {
        testFile = getTestDirFile(aTestFile.originalFile);
        testFile.copyToFollowingLinks(testDir, aTestFile.fileName);
        testFile = getApplyDirFile(aTestFile.relPathDir + aTestFile.fileName);
        Assert.ok(
          testFile.exists(),
          MSG_SHOULD_EXIST + ", path: " + testFile.path
        );
      } else {
        testFile = getApplyDirFile(aTestFile.relPathDir + aTestFile.fileName);
        writeFile(testFile, aTestFile.originalContents);
        if (
          AppConstants.platform == "macosx" &&
          aTestFile.originalContents == aTestFile.compareContents
        ) {
          // Self-signing the app bundle later during setupUpdaterTest can
          // modify files, hence we need to wait to set the compareContents
          // property until we know what to expect.
          filesWithCompareContents.push({
            memFile: aTestFile,
            diskFile: testFile,
          });
        }
      }

      // Skip these tests on Windows since chmod doesn't really set permissions
      // on Windows.
      if (AppConstants.platform != "win" && aTestFile.originalPerms) {
        testFile.permissions = aTestFile.originalPerms;
        // Store the actual permissions on the file for reference later after
        // setting the permissions.
        if (!aTestFile.comparePerms) {
          aTestFile.comparePerms = testFile.permissions;
        }
      }
    } else if (aTestFile.existingFile) {
      const testFile = getApplyDirFile(
        aTestFile.relPathDir + aTestFile.fileName
      );
      if (aTestFile.removeOriginalFile) {
        testFile.remove(false);
      } else {
        const fileContents = readFileBytes(testFile);
        if (!aTestFile.originalContents && !aTestFile.originalFile) {
          aTestFile.originalContents = fileContents;
        }
        if (!aTestFile.compareContents && !aTestFile.compareFile) {
          if (AppConstants.platform == "macosx") {
            // Self-signing the app bundle later during setupUpdaterTest can
            // modify files, hence we need to wait to set the compareContents
            // property until we know what to expect.
            filesWithCompareContents.push({
              memFile: aTestFile,
              diskFile: testFile,
            });
          } else {
            aTestFile.compareContents = fileContents;
          }
        }
        if (!aTestFile.comparePerms) {
          aTestFile.comparePerms = testFile.permissions;
        }
      }
    }
    debugDump("finish - setup test file: " + aTestFile.fileName);
  });

  // Add the test directory that will be updated for a successful update or left
  // in the initial state for a failed update.
  gTestDirs.forEach(function SUT_TD_FE(aTestDir) {
    debugDump("start - setup test directory: " + aTestDir.relPathDir);
    let testDir = getApplyDirFile(aTestDir.relPathDir);
    // Somehow these create calls are failing with FILE_ALREADY_EXISTS even
    // after checking .exists() first, so we just catch the exception.
    try {
      testDir.create(Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
    } catch (e) {
      if (e.result != Cr.NS_ERROR_FILE_ALREADY_EXISTS) {
        throw e;
      }
    }

    if (aTestDir.files) {
      aTestDir.files.forEach(function SUT_TD_F_FE(aTestFile) {
        let testFile = getApplyDirFile(aTestDir.relPathDir + aTestFile);
        if (!testFile.exists()) {
          testFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
        }
      });
    }

    if (aTestDir.subDirs) {
      aTestDir.subDirs.forEach(function SUT_TD_SD_FE(aSubDir) {
        let testSubDir = getApplyDirFile(aTestDir.relPathDir + aSubDir);
        if (!testSubDir.exists()) {
          testSubDir.create(Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
        }

        if (aTestDir.subDirFiles) {
          aTestDir.subDirFiles.forEach(function SUT_TD_SDF_FE(aTestFile) {
            let testFile = getApplyDirFile(
              aTestDir.relPathDir + aSubDir + aTestFile
            );
            if (!testFile.exists()) {
              testFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
            }
          });
        }
      });
    }
    debugDump("finish - setup test directory: " + aTestDir.relPathDir);
  });

  if (aSetupActiveUpdate) {
    await setupActiveUpdate();
  }

  if (aPostUpdateAsync !== null) {
    createUpdaterINI(aPostUpdateAsync, aPostUpdateExeRelPathPrefix, {
      asyncExeArg,
    });
  }

  await TestUtils.waitForCondition(() => {
    try {
      setupAppFiles({ requiresOmnijar });
      return true;
    } catch (e) {
      logTestInfo("exception when calling setupAppFiles, Exception: " + e);
    }
    return false;
  }, "Waiting to setup app files");

  if (AppConstants.platform == "macosx") {
    let bundlePath = afterApplyBinDir.parent.parent.path;
    stripQuarantineBitFromPath(bundlePath);
    selfSignAppBundle(bundlePath);
    filesWithCompareContents.forEach(elem => {
      elem.memFile.originalContents = readFileBytes(elem.diskFile);
      elem.memFile.compareContents = elem.memFile.originalContents;
    });
    // Set a similar extended attribute on the `.app` directory as we see in
    // the wild. We will verify that it is preserved at the end of tests.
    await IOUtils.setMacXAttr(
      getApplyDirFile().path,
      MAC_APP_XATTR_KEY,
      new TextEncoder().encode(MAC_APP_XATTR_VALUE)
    );
  }

  debugDump("finish - updater test setup");
}

/**
 * Helper function for updater binary tests that creates the updater.ini
 * file.
 *
 * @param   aIsExeAsync
 *          True or undefined if the post update process should be async. If
 *          undefined ExeAsync will not be added to the updater.ini file in
 *          order to test the default launch behavior which is async.
 * @param   aExeRelPathPrefix
 *          A string to prefix the ExeRelPath values in the updater.ini.
 * @param   options.asyncExeArg
 *          When `aIsExeAsync`, the (single) argument to invoke the
 *          post-update process with.  Default: "post-update-async".
 */
function createUpdaterINI(
  aIsExeAsync,
  aExeRelPathPrefix,
  { asyncExeArg = "post-update-async" } = {}
) {
  let exeArg = `ExeArg=${asyncExeArg}\n`;
  let exeAsync = "";
  if (aIsExeAsync !== undefined) {
    if (aIsExeAsync) {
      exeAsync = "ExeAsync=true\n";
    } else {
      exeArg = "ExeArg=post-update-sync\n";
      exeAsync = "ExeAsync=false\n";
    }
  }

  if (AppConstants.platform == "win" && aExeRelPathPrefix) {
    aExeRelPathPrefix = aExeRelPathPrefix.replace("/", "\\");
  }

  let exeRelPathMac =
    "ExeRelPath=" + aExeRelPathPrefix + DIR_MACOS + gPostUpdateBinFile + "\n";
  let exeRelPathWin =
    "ExeRelPath=" + aExeRelPathPrefix + gPostUpdateBinFile + "\n";
  let updaterIniContents =
    "[Strings]\n" +
    "Title=Update Test\n" +
    "Info=Running update test " +
    gTestID +
    "\n\n" +
    "[PostUpdateMac]\n" +
    exeRelPathMac +
    exeArg +
    exeAsync +
    "\n" +
    "[PostUpdateWin]\n" +
    exeRelPathWin +
    exeArg +
    exeAsync;
  let updaterIni = getApplyDirFile(DIR_RESOURCES + FILE_UPDATER_INI);
  writeFile(updaterIni, updaterIniContents);
}

/**
 * Gets the message log path used for assert checks to lessen the length printed
 * to the log file.
 *
 * @param   aPath
 *          The path to shorten for the log file.
 * @return  the message including the shortened path for the log file.
 */
function getMsgPath(aPath) {
  return ", path: " + replaceLogPaths(aPath);
}

/**
 * Helper function that replaces the common part of paths in the update log's
 * contents with <test_dir_path> for paths to the the test directory and
 * <update_dir_path> for paths to the update directory. This is needed since
 * Assert.equal will truncate what it prints to the xpcshell log file.
 *
 * @param   aLogContents
 *          The update log file's contents.
 * @return  the log contents with the paths replaced.
 */
function replaceLogPaths(aLogContents) {
  let logContents = aLogContents;
  // Remove the majority of the path up to the test directory. This is needed
  // since Assert.equal won't print long strings to the test logs.
  let testDirPath = getApplyDirFile().parent.path;
  if (AppConstants.platform == "win") {
    // Replace \\ with \\\\ so the regexp works.
    testDirPath = testDirPath.replace(/\\/g, "\\\\");
  }
  logContents = logContents.replace(
    new RegExp(testDirPath, "g"),
    "<test_dir_path>/" + gTestID
  );
  let updatesDirPath = getMockUpdRootD().path;
  if (AppConstants.platform == "win") {
    // Replace \\ with \\\\ so the regexp works.
    updatesDirPath = updatesDirPath.replace(/\\/g, "\\\\");
  }
  logContents = logContents.replace(
    new RegExp(updatesDirPath, "g"),
    "<update_dir_path>/" + gTestID
  );
  if (AppConstants.platform == "win") {
    // Replace \ with /
    logContents = logContents.replace(/\\/g, "/");
  }
  return logContents;
}

/**
 * Helper function that removes the timestamps in the update log
 *
 * @param   aLogContents
 *          The update log file's contents.
 * @return  the log contents without timestamps
 */
function removeTimeStamps(aLogContents) {
  return aLogContents.replace(
    /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}[+-]\d{4}: /gm,
    ""
  );
}

/**
 * Helper function that gets the contents of the last update log.
 *
 * It used to be that we only kept one copy of the updater log. This would be
 * the copy created by the elevated updater, if it was run. If it wasn't run,
 * then then it would be the only copy (created by the unelevated updater).
 * Now we keep both of these files. These tests were written assuming that
 * this unelevated updater log would be overwritten if the updater ran
 * elevated. Since that is no longer true, we can get the correct log intended
 * by these tests by always just trying for the elevated version first and, if
 * that doesn't exist, getting the unelevated version.
 * This works better than checking `gIsServiceTest` because some service tests
 * intentionally run bits of the test without elevation.
 */
function getLogFileContents() {
  let updateLog = getUpdateDirFile(FILE_LAST_UPDATE_ELEVATED_LOG);
  let updateLogContents;
  try {
    updateLogContents = readFileBytes(updateLog);
  } catch (ex) {
    if (ex.result != Cr.NS_ERROR_FILE_NOT_FOUND) {
      throw ex;
    }
    updateLog = getUpdateDirFile(FILE_LAST_UPDATE_LOG);
    updateLogContents = readFileBytes(updateLog);
  }
  return updateLogContents;
}

/**
 * Helper function for updater binary tests for verifying the contents of the
 * update log after a successful update.
 * Requires that the compare file have all the correct log lines in the correct
 * order, but it is not an error for extra lines to be present in the test file.
 *
 * @param   aCompareLogFile
 *          The log file to compare the update log with.
 * @param   aStaged
 *          If the update log file is for a staged update.
 * @param   aReplace
 *          If the update log file is for a replace update.
 * @param   aExcludeDistDir
 *          Removes lines containing the distribution directory from the log
 *          file to compare the update log with.
 */
function checkUpdateLogContents(
  aCompareLogFile,
  aStaged = false,
  aReplace = false,
  aExcludeDistDir = false
) {
  if (AppConstants.platform == "macosx" || AppConstants.platform == "linux") {
    // The order that files are returned when enumerating the file system on
    // Linux and Mac is not deterministic so skip checking the logs.
    return;
  }

  let updateLogContents = getLogFileContents();

  // Remove leading timestamps
  updateLogContents = removeTimeStamps(updateLogContents);

  const channelPrefs = getTestFileByName(FILE_CHANNEL_PREFS);
  if (channelPrefs && !channelPrefs.originalContents) {
    updateLogContents = updateLogContents.replace(/.*defaults\/.*/g, "");
  }

  const updateSettings = getTestFileByName(FILE_UPDATE_SETTINGS_INI);
  if (updateSettings && !updateSettings.originalContents) {
    updateLogContents = updateLogContents.replace(
      /.*update-settings.ini.*/g,
      ""
    );
  }

  // Skip the source/destination lines since they contain absolute paths.
  // These could be changed to relative paths using <test_dir_path> and
  // <update_dir_path>
  updateLogContents = updateLogContents.replace(/PATCH DIRECTORY.*/g, "");
  updateLogContents = updateLogContents.replace(
    /INSTALLATION DIRECTORY.*/g,
    ""
  );
  updateLogContents = updateLogContents.replace(/WORKING DIRECTORY.*/g, "");
  // Skip lines that log failed attempts to open the callback executable.
  updateLogContents = updateLogContents.replace(
    /NS_main: callback app file .*/g,
    ""
  );
  // Remove carriage returns.
  updateLogContents = updateLogContents.replace(/\r/g, "");

  if (AppConstants.platform == "win") {
    // The FindFile results when enumerating the filesystem on Windows is not
    // determistic so the results matching the following need to be fixed.
    let re = new RegExp(
      // eslint-disable-next-line no-control-regex
      "([^\n]* 7/7text1[^\n]*)\n([^\n]* 7/7text0[^\n]*)\n",
      "g"
    );
    updateLogContents = updateLogContents.replace(re, "$2\n$1\n");
  }

  if (aReplace) {
    // Remove the lines which contain absolute paths
    updateLogContents = updateLogContents.replace(/^Begin moving.*$/gm, "");
    updateLogContents = updateLogContents.replace(
      /^ensure_remove: failed to remove file: .*$/gm,
      ""
    );
    updateLogContents = updateLogContents.replace(
      /^ensure_remove_recursive: unable to remove directory: .*$/gm,
      ""
    );
    updateLogContents = updateLogContents.replace(
      /^Removing tmpDir failed, err: -1$/gm,
      ""
    );
    updateLogContents = updateLogContents.replace(
      /^remove_recursive_on_reboot: .*$/gm,
      ""
    );
    // Replace requests will retry renaming the installation directory 10 times
    // when there are files still in use. The following will remove the
    // additional entries from the log file when this happens so the log check
    // passes.
    let re = new RegExp(
      ERR_RENAME_FILE +
        "[^\n]*\n" +
        "PerformReplaceRequest: destDir rename[^\n]*\n" +
        "rename_file: proceeding to rename the directory\n",
      "g"
    );
    updateLogContents = updateLogContents.replace(re, "");
  }

  // Replace error codes since they are different on each platform.
  updateLogContents = updateLogContents.replace(/, err:.*\n/g, "\n");
  // Replace to make the log parsing happy.
  updateLogContents = updateLogContents.replace(/non-fatal error /g, "");
  // Remove consecutive newlines
  updateLogContents = updateLogContents.replace(/\n+/g, "\n");
  // Remove leading and trailing newlines
  updateLogContents = updateLogContents.replace(/^\n|\n$/g, "");
  // Replace the log paths with <test_dir_path> and <update_dir_path>
  updateLogContents = replaceLogPaths(updateLogContents);

  let compareLogContents = "";
  if (aCompareLogFile) {
    compareLogContents = readFileBytes(getTestDirFile(aCompareLogFile));
  }

  if (aStaged) {
    compareLogContents = PERFORMING_STAGED_UPDATE + "\n" + compareLogContents;
  }

  // Remove leading timestamps
  compareLogContents = removeTimeStamps(compareLogContents);

  if (channelPrefs && !channelPrefs.originalContents) {
    compareLogContents = compareLogContents.replace(/.*defaults\/.*/g, "");
  }

  if (updateSettings && !updateSettings.originalContents) {
    compareLogContents = compareLogContents.replace(
      /.*update-settings.ini.*/g,
      ""
    );
  }

  if (aExcludeDistDir) {
    compareLogContents = compareLogContents.replace(/.*distribution\/.*/g, "");
  }

  // Remove leading and trailing newlines
  compareLogContents = compareLogContents.replace(/\n+/g, "\n");
  // Remove leading and trailing newlines
  compareLogContents = compareLogContents.replace(/^\n|\n$/g, "");

  // Compare line by line, skipping non-matching lines that may be in the update
  // log so that these tests don't start failing just because we add a new log
  // message to the updater.
  let compareLogContentsArray = compareLogContents.split("\n");
  let updateLogContentsArray = updateLogContents.split("\n");
  while (updateLogContentsArray.length && compareLogContentsArray.length) {
    if (updateLogContentsArray[0] == compareLogContentsArray[0]) {
      compareLogContentsArray.shift();
    }
    updateLogContentsArray.shift();
  }

  // Don't write the contents of the file to the log to reduce log spam
  // unless there is a failure.
  if (!compareLogContentsArray.length) {
    Assert.ok(true, "the update log contents" + MSG_SHOULD_EQUAL);
  } else {
    Assert.ok(
      false,
      `the update log is missing the line: '${compareLogContentsArray[0]}'`
    );
  }
}

/**
 * Helper function to check if the update log contains a string.
 *
 * @param   aCheckString
 *          The string to check if the update log contains.
 */
function checkUpdateLogContains(aCheckString) {
  let updateLogContents = getLogFileContents();
  updateLogContents = updateLogContents.replace(/\r\n/g, "\n");
  updateLogContents = removeTimeStamps(updateLogContents);
  updateLogContents = replaceLogPaths(updateLogContents);

  // Compare line by line, skipping non-matching lines that may be in the update
  // log so that these tests don't start failing just because we add a new log
  // message to the updater.
  let isFirstCompareLine = true;
  let compareLogContentsArray = aCheckString.split("\n");
  let updateLogContentsArray = updateLogContents.split("\n");
  while (updateLogContentsArray.length && compareLogContentsArray.length) {
    let isLastCompareLine = compareLogContentsArray.length == 1;
    if (isFirstCompareLine && isLastCompareLine) {
      if (updateLogContentsArray[0].includes(compareLogContentsArray[0])) {
        compareLogContentsArray.shift();
        isFirstCompareLine = false;
      }
    } else if (isFirstCompareLine) {
      if (updateLogContentsArray[0].endsWith(compareLogContentsArray[0])) {
        compareLogContentsArray.shift();
        isFirstCompareLine = false;
      }
    } else if (isLastCompareLine) {
      if (updateLogContentsArray[0].startsWith(compareLogContentsArray[0])) {
        compareLogContentsArray.shift();
      }
    } else if (updateLogContentsArray[0] == compareLogContentsArray[0]) {
      compareLogContentsArray.shift();
    }
    updateLogContentsArray.shift();
  }

  if (!compareLogContentsArray.length) {
    Assert.ok(true, "the update log contents" + MSG_SHOULD_EQUAL);
  } else {
    Assert.ok(
      false,
      `the update log is missing the line: '${compareLogContentsArray[0]}'`
    );
  }
}

/**
 * Helper function for updater binary tests for verifying the state of files and
 * directories after a successful update.
 *
 * @param   aGetFileFunc
 *          The function used to get the files in the directory to be checked.
 * @param   aStageDirExists
 *          If true the staging directory will be tested for existence and if
 *          false the staging directory will be tested for non-existence.
 * @param   aToBeDeletedDirExists
 *          On Windows, if true the tobedeleted directory will be tested for
 *          existence and if false the tobedeleted directory will be tested for
 *          non-existence. On all othere platforms it will be tested for
 *          non-existence.
 */
function checkFilesAfterUpdateSuccess(
  aGetFileFunc,
  aStageDirExists = false,
  aToBeDeletedDirExists = false
) {
  debugDump("testing contents of files after a successful update");
  gTestFiles.forEach(function CFAUS_TF_FE(aTestFile) {
    let testFile = aGetFileFunc(
      aTestFile.relPathDir + aTestFile.fileName,
      true
    );
    debugDump("testing file: " + testFile.path);
    if (aTestFile.compareFile || aTestFile.compareContents) {
      Assert.ok(
        testFile.exists(),
        MSG_SHOULD_EXIST + getMsgPath(testFile.path)
      );
      if (
        !aTestFile.skipFileVerification ||
        !aTestFile.skipFileVerification.includes(AppConstants.platform)
      ) {
        // Skip these tests on Windows since chmod doesn't really set permissions
        // on Windows.
        if (AppConstants.platform != "win" && aTestFile.comparePerms) {
          // Check if the permssions as set in the complete mar file are correct.
          Assert.equal(
            "0o" + (testFile.permissions & 0xfff).toString(8),
            "0o" + (aTestFile.comparePerms & 0xfff).toString(8),
            "the file permissions" + MSG_SHOULD_EQUAL
          );
        }

        let fileContents1 = readFileBytes(testFile);
        let fileContents2 = aTestFile.compareFile
          ? readFileBytes(getTestDirFile(aTestFile.compareFile))
          : aTestFile.compareContents;

        // Don't write the contents of the file to the log to reduce log spam
        // unless there is a failure.
        if (fileContents1 == fileContents2) {
          Assert.ok(true, "the file contents" + MSG_SHOULD_EQUAL);
        } else {
          Assert.equal(
            fileContents1,
            fileContents2,
            "the file contents" + MSG_SHOULD_EQUAL
          );
        }
      }
    } else {
      Assert.ok(
        !testFile.exists(),
        MSG_SHOULD_NOT_EXIST + getMsgPath(testFile.path)
      );
    }
  });

  debugDump(
    "testing operations specified in removed-files were performed " +
      "after a successful update"
  );
  gTestDirs.forEach(function CFAUS_TD_FE(aTestDir) {
    let testDir = aGetFileFunc(aTestDir.relPathDir, true);
    debugDump("testing directory: " + testDir.path);
    if (aTestDir.dirRemoved) {
      Assert.ok(
        !testDir.exists(),
        MSG_SHOULD_NOT_EXIST + getMsgPath(testDir.path)
      );
    } else {
      Assert.ok(testDir.exists(), MSG_SHOULD_EXIST + getMsgPath(testDir.path));

      if (aTestDir.files) {
        aTestDir.files.forEach(function CFAUS_TD_F_FE(aTestFile) {
          let testFile = aGetFileFunc(aTestDir.relPathDir + aTestFile, true);
          if (aTestDir.filesRemoved) {
            Assert.ok(
              !testFile.exists(),
              MSG_SHOULD_NOT_EXIST + getMsgPath(testFile.path)
            );
          } else {
            Assert.ok(
              testFile.exists(),
              MSG_SHOULD_EXIST + getMsgPath(testFile.path)
            );
          }
        });
      }

      if (aTestDir.subDirs) {
        aTestDir.subDirs.forEach(function CFAUS_TD_SD_FE(aSubDir) {
          let testSubDir = aGetFileFunc(aTestDir.relPathDir + aSubDir, true);
          Assert.ok(
            testSubDir.exists(),
            MSG_SHOULD_EXIST + getMsgPath(testSubDir.path)
          );
          if (aTestDir.subDirFiles) {
            aTestDir.subDirFiles.forEach(function CFAUS_TD_SDF_FE(aTestFile) {
              let testFile = aGetFileFunc(
                aTestDir.relPathDir + aSubDir + aTestFile,
                true
              );
              Assert.ok(
                testFile.exists(),
                MSG_SHOULD_EXIST + getMsgPath(testFile.path)
              );
            });
          }
        });
      }
    }
  });

  if (AppConstants.platform == "macosx") {
    debugDump("testing that xattrs were preserved after a successful update");
    IOUtils.getMacXAttr(getApplyDirFile().path, MAC_APP_XATTR_KEY).then(
      bytes => {
        Assert.equal(
          new TextDecoder().decode(bytes),
          MAC_APP_XATTR_VALUE,
          "xattr value changed"
        );
      },
      _reason => {
        Assert.fail(MAC_APP_XATTR_KEY + " xattr is missing!");
      }
    );
  }

  checkFilesAfterUpdateCommon(aStageDirExists, aToBeDeletedDirExists);
}

/**
 * Helper function for updater binary tests for verifying the state of files and
 * directories after a failed update.
 *
 * @param   aGetFileFunc
 *          The function used to get the files in the directory to be checked.
 * @param   aStageDirExists
 *          If true the staging directory will be tested for existence and if
 *          false the staging directory will be tested for non-existence.
 * @param   aToBeDeletedDirExists
 *          On Windows, if true the tobedeleted directory will be tested for
 *          existence and if false the tobedeleted directory will be tested for
 *          non-existence. On all othere platforms it will be tested for
 *          non-existence.
 */
function checkFilesAfterUpdateFailure(
  aGetFileFunc,
  aStageDirExists = false,
  aToBeDeletedDirExists = false
) {
  debugDump("testing contents of files after a failed update");
  gTestFiles.forEach(function CFAUF_TF_FE(aTestFile) {
    let testFile = aGetFileFunc(
      aTestFile.relPathDir + aTestFile.fileName,
      true
    );
    debugDump("testing file: " + testFile.path);
    if (aTestFile.compareFile || aTestFile.compareContents) {
      Assert.ok(
        testFile.exists(),
        MSG_SHOULD_EXIST + getMsgPath(testFile.path)
      );
      if (
        !aTestFile.skipFileVerification ||
        !aTestFile.skipFileVerification.includes(AppConstants.platform)
      ) {
        // Skip these tests on Windows since chmod doesn't really set permissions
        // on Windows.
        if (AppConstants.platform != "win" && aTestFile.comparePerms) {
          // Check the original permssions are retained on the file.
          Assert.equal(
            testFile.permissions & 0xfff,
            aTestFile.comparePerms & 0xfff,
            "the file permissions" + MSG_SHOULD_EQUAL
          );
        }

        let fileContents1 = readFileBytes(testFile);
        let fileContents2 = aTestFile.compareFile
          ? readFileBytes(getTestDirFile(aTestFile.compareFile))
          : aTestFile.compareContents;

        // Don't write the contents of the file to the log to reduce log spam
        // unless there is a failure.
        if (fileContents1 == fileContents2) {
          Assert.ok(true, "the file contents" + MSG_SHOULD_EQUAL);
        } else {
          Assert.equal(
            fileContents1,
            fileContents2,
            "the file contents" + MSG_SHOULD_EQUAL
          );
        }
      }
    } else {
      Assert.ok(
        !testFile.exists(),
        MSG_SHOULD_NOT_EXIST + getMsgPath(testFile.path)
      );
    }
  });

  debugDump(
    "testing operations specified in removed-files were not " +
      "performed after a failed update"
  );
  gTestDirs.forEach(function CFAUF_TD_FE(aTestDir) {
    let testDir = aGetFileFunc(aTestDir.relPathDir, true);
    Assert.ok(testDir.exists(), MSG_SHOULD_EXIST + getMsgPath(testDir.path));

    if (aTestDir.files) {
      aTestDir.files.forEach(function CFAUS_TD_F_FE(aTestFile) {
        let testFile = aGetFileFunc(aTestDir.relPathDir + aTestFile, true);
        Assert.ok(
          testFile.exists(),
          MSG_SHOULD_EXIST + getMsgPath(testFile.path)
        );
      });
    }

    if (aTestDir.subDirs) {
      aTestDir.subDirs.forEach(function CFAUS_TD_SD_FE(aSubDir) {
        let testSubDir = aGetFileFunc(aTestDir.relPathDir + aSubDir, true);
        Assert.ok(
          testSubDir.exists(),
          MSG_SHOULD_EXIST + getMsgPath(testSubDir.path)
        );
        if (aTestDir.subDirFiles) {
          aTestDir.subDirFiles.forEach(function CFAUS_TD_SDF_FE(aTestFile) {
            let testFile = aGetFileFunc(
              aTestDir.relPathDir + aSubDir + aTestFile,
              true
            );
            Assert.ok(
              testFile.exists(),
              MSG_SHOULD_EXIST + getMsgPath(testFile.path)
            );
          });
        }
      });
    }
  });

  checkFilesAfterUpdateCommon(aStageDirExists, aToBeDeletedDirExists);
}

/**
 * Helper function for updater binary tests for verifying the state of common
 * files and directories after a successful or failed update.
 *
 * @param   aStageDirExists
 *          If true the staging directory will be tested for existence and if
 *          false the staging directory will be tested for non-existence.
 * @param   aToBeDeletedDirExists
 *          On Windows, if true the tobedeleted directory will be tested for
 *          existence and if false the tobedeleted directory will be tested for
 *          non-existence. On all othere platforms it will be tested for
 *          non-existence.
 */
function checkFilesAfterUpdateCommon(aStageDirExists, aToBeDeletedDirExists) {
  debugDump("testing extra directories");
  let stageDir = getStageDirFile();
  if (aStageDirExists) {
    Assert.ok(stageDir.exists(), MSG_SHOULD_EXIST + getMsgPath(stageDir.path));
  } else {
    Assert.ok(
      !stageDir.exists(),
      MSG_SHOULD_NOT_EXIST + getMsgPath(stageDir.path)
    );
  }

  let toBeDeletedDirExists =
    AppConstants.platform == "win" ? aToBeDeletedDirExists : false;
  let toBeDeletedDir = getApplyDirFile(DIR_TOBEDELETED);
  if (toBeDeletedDirExists) {
    Assert.ok(
      toBeDeletedDir.exists(),
      MSG_SHOULD_EXIST + getMsgPath(toBeDeletedDir.path)
    );
  } else {
    Assert.ok(
      !toBeDeletedDir.exists(),
      MSG_SHOULD_NOT_EXIST + getMsgPath(toBeDeletedDir.path)
    );
  }

  let updatingDir = getApplyDirFile("updating");
  Assert.ok(
    !updatingDir.exists(),
    MSG_SHOULD_NOT_EXIST + getMsgPath(updatingDir.path)
  );

  if (stageDir.exists()) {
    updatingDir = stageDir.clone();
    updatingDir.append("updating");
    Assert.ok(
      !updatingDir.exists(),
      MSG_SHOULD_NOT_EXIST + getMsgPath(updatingDir.path)
    );
  }

  debugDump(
    "testing backup files should not be left behind in the " +
      "application directory"
  );
  let applyToDir = getApplyDirFile();
  checkFilesInDirRecursive(applyToDir, checkForBackupFiles);

  if (stageDir.exists()) {
    debugDump(
      "testing backup files should not be left behind in the " +
        "staging directory"
    );
    checkFilesInDirRecursive(stageDir, checkForBackupFiles);
  }
}

/**
 * Helper function for updater binary tests for verifying the contents of the
 * updater callback application log which should contain the arguments passed to
 * the callback application.
 *
 * @param appLaunchLog (optional)
 *        The application log nsIFile to verify.  Defaults to the second
 *        parameter passed to the callback executable (in the apply directory).
 */
function checkCallbackLog(
  appLaunchLog = getApplyDirFile(DIR_MACOS + gCallbackArgs[1])
) {
  if (!appLaunchLog.exists()) {
    debugDump("Callback log does not exist yet. Path: " + appLaunchLog.path);
    // Uses do_timeout instead of do_execute_soon to lessen log spew.
    do_timeout(FILE_IN_USE_TIMEOUT_MS, checkCallbackLog);
    return;
  }

  let expectedLogContents = gCallbackArgs.join("\n") + "\n";
  let logContents = readFile(appLaunchLog);
  // It is possible for the log file contents check to occur before the log file
  // contents are completely written so wait until the contents are the expected
  // value. If the contents are never the expected value then the test will
  // fail by timing out after gTimeoutRuns is greater than MAX_TIMEOUT_RUNS or
  // the test harness times out the test.
  const MAX_TIMEOUT_RUNS = 20000;
  if (logContents != expectedLogContents) {
    gTimeoutRuns++;
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
      logTestInfo("callback log contents are not correct");
      // This file doesn't contain full paths so there is no need to call
      // replaceLogPaths.
      let aryLog = logContents.split("\n");
      let aryCompare = expectedLogContents.split("\n");
      // Pushing an empty string to both arrays makes it so either array's length
      // can be used in the for loop below without going out of bounds.
      aryLog.push("");
      aryCompare.push("");
      // xpcshell tests won't display the entire contents so log the incorrect
      // line.
      for (let i = 0; i < aryLog.length; ++i) {
        if (aryLog[i] != aryCompare[i]) {
          logTestInfo(
            "the first incorrect line in the callback log is: " + aryLog[i]
          );
          Assert.equal(
            aryLog[i],
            aryCompare[i],
            "the callback log contents" + MSG_SHOULD_EQUAL
          );
        }
      }
      // This should never happen!
      do_throw("Unable to find incorrect callback log contents!");
    }
    // Uses do_timeout instead of do_execute_soon to lessen log spew.
    do_timeout(FILE_IN_USE_TIMEOUT_MS, checkCallbackLog);
    return;
  }
  Assert.ok(true, "the callback log contents" + MSG_SHOULD_EQUAL);

  waitForFilesInUse();
}

/**
 * Helper function for updater binary tests for getting the log and running
 * files created by the test helper binary file when called with the post-update
 * command line argument.
 *
 * @param   aSuffix
 *          The string to append to the post update test helper binary path.
 */
function getPostUpdateFile(aSuffix) {
  return getApplyDirFile(DIR_MACOS + gPostUpdateBinFile + aSuffix);
}

/**
 * Checks the contents of the updater post update binary log. When completed
 * checkPostUpdateAppLogFinished will be called.
 *
 * @param   options.expectedContents
 *          The expected log content.  Default: "post-update\n".
 */
async function checkPostUpdateAppLog({
  expectedContents = "post-update\n",
} = {}) {
  // Only Mac OS X and Windows support post update.
  if (AppConstants.platform == "macosx" || AppConstants.platform == "win") {
    let file = getPostUpdateFile(".log");
    await TestUtils.waitForCondition(
      () => file.exists(),
      "Waiting for file to exist, path: " + file.path
    );

    await TestUtils.waitForCondition(
      () => readFile(file) == expectedContents,
      // This is wonky: the message is evaluated _first_, not _finally_!  But
      // when there's a mismatch and not a race, it still has the final content.
      "Waiting for expected file contents: " +
        expectedContents +
        ", first read: " +
        readFile(file)
    );

    Assert.equal(
      readFile(file),
      expectedContents,
      "the post update log contents" + MSG_SHOULD_EQUAL
    );
  }
}

/**
 * Helper function to check if a file is in use on Windows by making a copy of
 * a file and attempting to delete the original file. If the deletion is
 * successful the copy of the original file is renamed to the original file's
 * name and if the deletion is not successful the copy of the original file is
 * deleted.
 *
 * @param   aFile
 *          An nsIFile for the file to be checked if it is in use.
 * @return  true if the file can't be deleted and false otherwise.
 * @throws  If called from a platform other than Windows.
 */
function isFileInUse(aFile) {
  if (AppConstants.platform != "win") {
    do_throw("Windows only function called by a different platform!");
  }

  if (!aFile.exists()) {
    debugDump("file does not exist, path: " + aFile.path);
    return false;
  }

  let fileBak = aFile.parent;
  fileBak.append(aFile.leafName + ".bak");
  try {
    if (fileBak.exists()) {
      fileBak.remove(false);
    }
    aFile.copyTo(aFile.parent, fileBak.leafName);
    aFile.remove(false);
    fileBak.moveTo(aFile.parent, aFile.leafName);
    debugDump("file is not in use, path: " + aFile.path);
    return false;
  } catch (e) {
    debugDump("file in use, path: " + aFile.path + ", Exception: " + e);
    try {
      if (fileBak.exists()) {
        fileBak.remove(false);
      }
    } catch (ex) {
      logTestInfo(
        "unable to remove backup file, path: " +
          fileBak.path +
          ", Exception: " +
          ex
      );
    }
  }
  return true;
}

/**
 * Waits until files that are in use that break tests are no longer in use and
 * then calls doTestFinish to end the test.
 */
async function waitForFilesInUse() {
  if (AppConstants.platform == "win") {
    let fileNames = [
      FILE_APP_BIN,
      FILE_UPDATER_BIN,
      FILE_MAINTENANCE_SERVICE_INSTALLER_BIN,
    ];
    for (let i = 0; i < fileNames.length; ++i) {
      let file = getApplyDirFile(fileNames[i]);
      if (isFileInUse(file)) {
        do_timeout(FILE_IN_USE_TIMEOUT_MS, waitForFilesInUse);
        return;
      }
    }
  }

  debugDump("calling doTestFinish");
  await doTestFinish();
}

/**
 * Helper function for updater binary tests for verifying there are no update
 * backup files left behind after an update.
 *
 * @param   aFile
 *          An nsIFile to check if it has moz-backup for its extension.
 */
function checkForBackupFiles(aFile) {
  Assert.notEqual(
    getFileExtension(aFile),
    "moz-backup",
    "the file's extension should not equal moz-backup" + getMsgPath(aFile.path)
  );
}

/**
 * Helper function for updater binary tests for recursively enumerating a
 * directory and calling a callback function with the file as a parameter for
 * each file found.
 *
 * @param   aDir
 *          A nsIFile for the directory to be deleted
 * @param   aCallback
 *          A callback function that will be called with the file as a
 *          parameter for each file found.
 */
function checkFilesInDirRecursive(aDir, aCallback) {
  if (!aDir.exists()) {
    do_throw("Directory must exist!");
  }

  let dirEntries = aDir.directoryEntries;
  while (dirEntries.hasMoreElements()) {
    let entry = dirEntries.nextFile;

    if (entry.exists()) {
      if (entry.isDirectory()) {
        checkFilesInDirRecursive(entry, aCallback);
      } else {
        aCallback(entry);
      }
    }
  }
}

/**
 * Waits for an update check request to complete and asserts that the results
 * are as-expected.
 *
 * @param   aSuccess
 *          Whether the update check succeeds or not. If aSuccess is true then
 *          the check should succeed and if aSuccess is false then the check
 *          should fail.
 * @param   aExpectedValues
 *          An object with common values to check.
 * @return  A promise which will resolve with the nsIUpdateCheckResult object
 *          once the update check is complete.
 */
async function waitForUpdateCheck(aSuccess, aExpectedValues = {}) {
  let check = gUpdateChecker.checkForUpdates(gUpdateChecker.FOREGROUND_CHECK);
  let result = await check.result;
  Assert.ok(result.checksAllowed, "We should be able to check for updates");
  Assert.equal(
    result.succeeded,
    aSuccess,
    "the update check should " + (aSuccess ? "succeed" : "error")
  );
  if (aExpectedValues.updateCount) {
    Assert.equal(
      aExpectedValues.updateCount,
      result.updates.length,
      "the update count" + MSG_SHOULD_EQUAL
    );
  }
  if (aExpectedValues.url) {
    Assert.equal(
      aExpectedValues.url,
      result.request.channel.originalURI.spec,
      "the url" + MSG_SHOULD_EQUAL
    );
  }
  return result;
}

/**
 * Downloads an update and waits for the download onStopRequest.
 *
 * @param   aUpdates
 *          An array of updates to select from to download an update.
 * @param   aUpdateCount
 *          The number of updates in the aUpdates array.
 * @param   aExpectedStatus
 *          The download onStopRequest expected status.
 * @return  A promise which will resolve the first time the update download
 *          onStopRequest occurs and returns the arguments from onStopRequest.
 */
async function waitForUpdateDownload(aUpdates, aExpectedStatus) {
  let bestUpdate = await gAUS.selectUpdate(aUpdates);
  let result = await gAUS.downloadUpdate(bestUpdate);
  if (result != Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS) {
    do_throw("nsIApplicationUpdateService:downloadUpdate returned " + result);
  }
  return new Promise(resolve =>
    gAUS.addDownloadListener({
      onStartRequest: _aRequest => {},
      onProgress: (_aRequest, _aContext, _aProgress, _aMaxProgress) => {},
      onStatus: (_aRequest, _aStatus, _aStatusText) => {},
      onStopRequest(request, status) {
        gAUS.removeDownloadListener(this);
        Assert.equal(
          aExpectedStatus,
          status,
          "the download status" + MSG_SHOULD_EQUAL
        );
        resolve(request, status);
      },
      QueryInterface: ChromeUtils.generateQI([
        "nsIRequestObserver",
        "nsIProgressEventSink",
      ]),
    })
  );
}

/**
 * Starts an update server to serve SJS scripts.
 *
 * A `registerCleanupFunction` call is made in this function to shut the server
 * down at the end of the test.
 *
 * Note that this serves a different purpose from from `start_httpserver`,
 * below. That server uses the very basic `pathHandler` handler, which basically
 * just serves whatever is in `gResponseBody`. This, in theory, serves arbitrary
 * SJS scripts. In practice, however, this is basically used to serve
 * `toolkit/mozapps/update/tests/data/app_update.sjs` to act as a rudimentary
 * update server.
 *
 * @param  options
 *         This function takes an optional options object that may include the
 *         following properties:
 *           onRequest
 *             If specified, this function will be called when the server
 *             handles a request. When invoked, it will be provided with a
 *             argument: the connection object.
 * @returns server
 *          The server that was started.
 */
function startSjsServer({ onResponse } = {}) {
  let { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );
  const server = new HttpServer();

  server.registerContentType("sjs", "sjs");
  server.registerDirectory("/", do_get_cwd());

  if (onResponse) {
    const origHandler = server._handler;
    server._handler = {
      handleResponse: connection => {
        onResponse(connection);
        return origHandler.handleResponse(connection);
      },
    };
  }

  server.start(-1);
  let port = server.identity.primaryPort;
  // eslint-disable-next-line no-global-assign
  gURLData =
    APP_UPDATE_SJS_HOST + ":" + port + APP_UPDATE_SJS_PATH + "?port=" + port;

  registerCleanupFunction(resolve => server.stop(resolve));

  return server;
}

/**
 * Helper for starting the http server used by the tests
 */
function start_httpserver() {
  let dir = getTestDirFile();
  debugDump("http server directory path: " + dir.path);

  if (!dir.isDirectory()) {
    do_throw(
      "A file instead of a directory was specified for HttpServer " +
        "registerDirectory! Path: " +
        dir.path
    );
  }

  let { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );
  gTestserver = new HttpServer();
  gTestserver.registerDirectory("/", dir);
  gTestserver.registerPathHandler("/" + gHTTPHandlerPath, pathHandler);
  gTestserver.start(-1);
  let testserverPort = gTestserver.identity.primaryPort;
  // eslint-disable-next-line no-global-assign
  gURLData = URL_HOST + ":" + testserverPort + "/";
  debugDump("http server port = " + testserverPort);
}

/**
 * Custom path handler for the http server
 *
 * @param   aMetadata
 *          The http metadata for the request.
 * @param   aResponse
 *          The http response for the request.
 */
function pathHandler(aMetadata, aResponse) {
  gUpdateCheckCount += 1;
  aResponse.setHeader("Content-Type", "text/xml", false);
  aResponse.setStatusLine(aMetadata.httpVersion, 200, "OK");
  aResponse.bodyOutputStream.write(gResponseBody, gResponseBody.length);
}

/**
 * Helper for stopping the http server used by the tests
 *
 * @param   aCallback
 *          The callback to call after stopping the http server.
 */
function stop_httpserver(aCallback) {
  Assert.ok(!!aCallback, "the aCallback parameter should be defined");
  gTestserver.stop(aCallback);
}

/**
 * Creates an nsIXULAppInfo
 *
 * @param   aID
 *          The ID of the test application
 * @param   aName
 *          A name for the test application
 * @param   aVersion
 *          The version of the application
 * @param   aPlatformVersion
 *          The gecko version of the application
 */
function createAppInfo(aID, aName, aVersion, aPlatformVersion) {
  updateAppInfo({
    vendor: APP_INFO_VENDOR,
    name: aName,
    ID: aID,
    version: aVersion,
    appBuildID: "2007010101",
    platformVersion: aPlatformVersion,
    platformBuildID: "2007010101",
    inSafeMode: false,
    logConsoleErrors: true,
    OS: "XPCShell",
    XPCOMABI: "noarch-spidermonkey",
  });
}

/**
 * Returns the platform specific arguments used by nsIProcess when launching
 * the application.
 *
 * @param   aExtraArgs (optional)
 *          An array of extra arguments to append to the default arguments.
 * @return  an array of arguments to be passed to nsIProcess.
 *
 * Note: a shell is necessary to pipe the application's console output which
 *       would otherwise pollute the xpcshell log.
 *
 * Command line arguments used when launching the application:
 * -test-process-updates makes the application exit after being relaunched by
 * the updater.
 * the platform specific string defined by PIPE_TO_NULL to output both stdout
 * and stderr to null. This is needed to prevent output from the application
 * from ending up in the xpchsell log.
 */
function getProcessArgs(aExtraArgs) {
  if (!aExtraArgs) {
    aExtraArgs = [];
  }

  let appBin = getApplyDirFile(DIR_MACOS + FILE_APP_BIN);
  Assert.ok(appBin.exists(), MSG_SHOULD_EXIST + ", path: " + appBin.path);
  let appBinPath = appBin.path;

  // The profile must be specified for the tests that launch the application to
  // run locally when the profiles.ini and installs.ini files already exist.
  // We can't use getApplyDirFile to find the profile path because on Windows
  // for service tests that would place the profile inside Program Files, and
  // this test script has permission to write in Program Files, but the
  // application may drop those permissions. So for Windows service tests we
  // override that path with the per-test temp directory that xpcshell provides,
  // which should be user writable.
  let profileDir = appBin.parent.parent;
  if (gIsServiceTest && IS_AUTHENTICODE_CHECK_ENABLED) {
    profileDir = do_get_tempdir();
  }
  profileDir.append("profile");
  let profilePath = profileDir.path;

  let args;
  if (AppConstants.platform == "macosx" || AppConstants.platform == "linux") {
    let launchScript = getLaunchScript();
    // Precreate the script with executable permissions
    launchScript.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_DIRECTORY);

    let scriptContents = "#! /bin/sh\n";
    scriptContents += "export XRE_PROFILE_PATH=" + profilePath + "\n";
    scriptContents +=
      appBinPath +
      " -test-process-updates " +
      aExtraArgs.join(" ") +
      " " +
      PIPE_TO_NULL;
    writeFile(launchScript, scriptContents);
    debugDump(
      "created " + launchScript.path + " containing:\n" + scriptContents
    );
    args = [launchScript.path];
  } else {
    args = [
      "/D",
      "/Q",
      "/C",
      appBinPath,
      "-profile",
      profilePath,
      "-test-process-updates",
      "-wait-for-browser",
    ]
      .concat(aExtraArgs)
      .concat([PIPE_TO_NULL]);
  }
  return args;
}

/**
 * Gets a file path for the application to dump its arguments into.  This is used
 * to verify that a callback application is launched.
 *
 * @return  the file for the application to dump its arguments into.
 */
function getAppArgsLogPath() {
  let appArgsLog = do_get_file("/" + gTestID + "_app_args_log", true);
  if (appArgsLog.exists()) {
    appArgsLog.remove(false);
  }
  let appArgsLogPath = appArgsLog.path;
  if (/ /.test(appArgsLogPath)) {
    appArgsLogPath = '"' + appArgsLogPath + '"';
  }
  return appArgsLogPath;
}

/**
 * Gets the nsIFile reference for the shell script to launch the application. If
 * the file exists it will be removed by this function.
 *
 * @return  the nsIFile for the shell script to launch the application.
 */
function getLaunchScript() {
  let launchScript = do_get_file("/" + gTestID + "_launch.sh", true);
  if (launchScript.exists()) {
    launchScript.remove(false);
  }
  return launchScript;
}

var gCustomGeneralPaths;
var gOriginalGeneralPaths;

// This function's source code is shared with child processes, so we try to
// minimize our dependency on things defined in this file. We currently only
// depend on gCustomGeneralPaths, we therefore forward this variable to child
// processes (see runInSubprocessWithPrelude).
function registerCustomDirProvider() {
  const nsFile = Components.Constructor(
    "@mozilla.org/file/local;1",
    "nsIFile",
    "initWithPath"
  );
  const dirProvider = {
    getFile: function AGP_DP_getFile(aProp, aPersistent) {
      // Set the value of persistent to false so when this directory provider is
      // unregistered it will revert back to the original provider.
      aPersistent.value = false;
      if (aProp in gCustomGeneralPaths) {
        return nsFile(gCustomGeneralPaths[aProp]);
      }
      return null;
    },
    QueryInterface: ChromeUtils.generateQI(["nsIDirectoryServiceProvider"]),
  };
  let ds = Services.dirsvc.QueryInterface(Ci.nsIDirectoryService);
  let props = ds.QueryInterface(Ci.nsIProperties);
  for (const prop of Object.keys(gCustomGeneralPaths)) {
    if (props.has(prop)) {
      props.undefine(prop);
    }
  }
  ds.registerProvider(dirProvider);

  return dirProvider;
}

// This function's source code is shared with child processes, which is OK
// because it does not depend on anything defined in this file.
function resetSyncManagerLock() {
  let syncManager = Cc["@mozilla.org/updates/update-sync-manager;1"].getService(
    Ci.nsIUpdateSyncManager
  );
  syncManager.resetLock();
}

// This function runs a child script in an xpcshell subprocess. We provide the
// child process with just enough of the source code of xpcshellUtilsAUS.js to
// let it recreate the environment that it would have if it were using
// adjustGeneralPaths(). The child script only really needs the ability to
// create the custom directory provider, and to reset the sync manager lock.

// We do this by passing a prelude that declares registerCustomDirProvider(),
// resetSyncManagerLock() and an already computed copy of gCustomGeneralPaths
// (as a dependency of registerCustomDirProvider). We rely on the fact that
// calling toString() on a JS function returns its source code. The child
// script must run these two functions on its own before doing its actual job.
//
// We do not want the child script to load the whole xpcshellUtilsAUS.js file,
// because it turns out that needs a bunch of infrastructure that normally the
// testing framework would provide, and that also requires a bunch of setup,
// and it's just not worth all that.
//
// We do not provide the child script with adjustGeneralPaths() directly,
// because that function is not self-contained and therefore harder to isolate.
// In particular, it registers a cleanup function AGP_cleanup() that does a lot
// of things that act upon the global state and are not particularly tied to
// what adjustGeneralPaths() itself does.
//
// The caller may use aExtraPrelude to provide extra JS code to run before the
// child script, so as to share extra data with it.
function runInSubprocessWithPrelude(aScriptPath, aExtraPrelude) {
  let prelude = `
const gCustomGeneralPaths = ${JSON.stringify(gCustomGeneralPaths)};
${registerCustomDirProvider.toString()}
${resetSyncManagerLock.toString()}
`;
  if (aExtraPrelude !== undefined) {
    prelude += aExtraPrelude;
  }
  const args = [
    "-g",
    gOriginalGeneralPaths[NS_GRE_DIR],
    "-e",
    prelude,
    "-f",
    aScriptPath,
  ];
  debugDump(
    `launching child process at ${gOriginalGeneralPaths[XRE_EXECUTABLE_FILE]} with args ${args}`
  );
  return Subprocess.call({
    command: gOriginalGeneralPaths[XRE_EXECUTABLE_FILE],
    arguments: args,
    stderr: "stdout",
  });
}

/**
 * Makes GreD, XREExeF, and UpdRootD point to unique file system locations so
 * xpcshell tests can run in parallel and to keep the environment clean.
 */
function adjustGeneralPaths() {
  gCustomGeneralPaths = {
    [NS_GRE_DIR]: getApplyDirFile(DIR_RESOURCES).path,
    [NS_GRE_BIN_DIR]: getApplyDirFile(DIR_MACOS).path,
    [XRE_EXECUTABLE_FILE]: getApplyDirFile(DIR_MACOS + FILE_APP_BIN).path,
  };

  gOriginalGeneralPaths = {};
  for (const prop of Object.keys(gCustomGeneralPaths)) {
    gOriginalGeneralPaths[prop] = Services.dirsvc.get(prop, Ci.nsIFile).path;
  }

  // Note: getMockUpdRootDMac uses gCustomGeneralPaths[XRE_EXECUTABLE_FILE]
  gCustomGeneralPaths[XRE_UPDATE_ROOT_DIR] = getMockUpdRootD().path;
  gCustomGeneralPaths[XRE_OLD_UPDATE_ROOT_DIR] = getMockUpdRootD(true).path;

  let dirProvider = registerCustomDirProvider();
  registerCleanupFunction(function AGP_cleanup() {
    debugDump("start - unregistering directory provider");

    if (gAppTimer) {
      debugDump("start - cancel app timer");
      gAppTimer.cancel();
      gAppTimer = null;
      debugDump("finish - cancel app timer");
    }

    if (gProcess && gProcess.isRunning) {
      debugDump("start - kill process");
      try {
        gProcess.kill();
      } catch (e) {
        debugDump("kill process failed, Exception: " + e);
      }
      gProcess = null;
      debugDump("finish - kill process");
    }

    if (gPIDPersistProcess && gPIDPersistProcess.isRunning) {
      debugDump("start - kill pid persist process");
      try {
        gPIDPersistProcess.kill();
      } catch (e) {
        debugDump("kill pid persist process failed, Exception: " + e);
      }
      gPIDPersistProcess = null;
      debugDump("finish - kill pid persist process");
    }

    if (gHandle) {
      try {
        debugDump("start - closing handle");
        let kernel32 = ctypes.open("kernel32");
        let CloseHandle = kernel32.declare(
          "CloseHandle",
          ctypes.winapi_abi,
          ctypes.bool /* return*/,
          ctypes.voidptr_t /* handle*/
        );
        if (!CloseHandle(gHandle)) {
          debugDump("call to CloseHandle failed");
        }
        kernel32.close();
        gHandle = null;
        debugDump("finish - closing handle");
      } catch (e) {
        debugDump("call to CloseHandle failed, Exception: " + e);
      }
    }

    let ds = Services.dirsvc.QueryInterface(Ci.nsIDirectoryService);
    ds.unregisterProvider(dirProvider);
    cleanupTestCommon();

    // Now that our provider is unregistered, reset the lock a second time so
    // that we know the lock we're interested in gets released (xpcshell
    // doesn't always run a proper XPCOM shutdown sequence, which is where that
    // would normally be happening).
    resetSyncManagerLock();

    debugDump("finish - unregistering directory provider");
  });
}

/**
 * The timer callback to kill the process if it takes too long.
 */
const gAppTimerCallback = {
  notify: function TC_notify(_aTimer) {
    gAppTimer = null;
    if (gProcess.isRunning) {
      logTestInfo("attempting to kill process");
      gProcess.kill();
    }
    Assert.ok(false, "launch application timer expired");
  },
  QueryInterface: ChromeUtils.generateQI(["nsITimerCallback"]),
};

/**
 * Launches an application to apply an update.
 *
 * @param   aExpectedStatus
 *          The expected value of update.status when the update finishes.
 */
async function runUpdateUsingApp(aExpectedStatus) {
  debugDump("start - launching application to apply update");

  // The maximum number of milliseconds the process that is launched can run
  // before the test will try to kill it.
  const APP_TIMER_TIMEOUT = 120000;
  let launchBin = getLaunchBin();
  let args = getProcessArgs();
  debugDump("launching " + launchBin.path + " " + args.join(" "));

  gProcess = Cc["@mozilla.org/process/util;1"].createInstance(Ci.nsIProcess);
  gProcess.init(launchBin);

  gAppTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  gAppTimer.initWithCallback(
    gAppTimerCallback,
    APP_TIMER_TIMEOUT,
    Ci.nsITimer.TYPE_ONE_SHOT
  );

  setEnvironment();

  debugDump("launching application");
  gProcess.run(true, args, args.length);
  debugDump("launched application exited");

  if (gAppTimer) {
    gAppTimer.cancel();
    gAppTimer = null;
  }

  resetEnvironment();
  const pidFile = getUpdateDirFile(FILE_TEST_PROCESS_UPDATES);
  debugDump(`Waiting for file: ${pidFile.path}`);
  let pid;
  await TestUtils.waitForCondition(
    () => {
      let fileContents = readFile(pidFile);
      if (!fileContents) {
        return false;
      }
      pid = parseInt(fileContents, 10);
      return !isNaN(pid);
    },
    "Waiting for application to write test complete file",
    /* interval */ 500
  );
  waitForPidStop(pid);
  try {
    pid.remove(false);
  } catch (ex) {
    debugDump("Failed to remove pid file", ex);
  }

  let file = getUpdateDirFile(FILE_UPDATE_STATUS);
  await TestUtils.waitForCondition(
    () => file.exists(),
    "Waiting for file to exist, path: " + file.path
  );

  await TestUtils.waitForCondition(
    () => readStatusFile() == aExpectedStatus,
    "Waiting for expected status file contents: " + aExpectedStatus
  ).catch(e => {
    // Instead of throwing let the check below fail the test so the status
    // file's contents are logged.
    logTestInfo(e);
  });
  Assert.equal(
    readStatusFile(),
    aExpectedStatus,
    "the status file state" + MSG_SHOULD_EQUAL
  );

  // Don't check for an update log when the code in nsUpdateDriver.cpp skips
  // updating.
  if (
    aExpectedStatus != STATE_PENDING &&
    aExpectedStatus != STATE_PENDING_SVC &&
    aExpectedStatus != STATE_APPLIED &&
    aExpectedStatus != STATE_APPLIED_SVC
  ) {
    // Don't proceed until the update log has been created.
    file = getUpdateDirFile(FILE_UPDATE_LOG);
    await TestUtils.waitForCondition(
      () => file.exists(),
      "Waiting for file to exist, path: " + file.path
    );
  }

  debugDump("finish - launching application to apply update");
}

/* This Mock incremental downloader is used to verify that connection interrupts
 * work correctly in updater code. The implementation of the mock incremental
 * downloader is very simple, it simply copies the file to the destination
 * location.
 */
function initMockIncrementalDownload() {
  const INC_CONTRACT_ID = "@mozilla.org/network/incremental-download;1";
  let incrementalDownloadCID = MockRegistrar.register(
    INC_CONTRACT_ID,
    IncrementalDownload
  );
  registerCleanupFunction(() => {
    MockRegistrar.unregister(incrementalDownloadCID);
  });
}

function IncrementalDownload() {
  this.wrappedJSObject = this;
}

IncrementalDownload.prototype = {
  /* nsIIncrementalDownload */
  init(uri, file, _chunkSize, _intervalInSeconds) {
    this._destination = file;
    this._URI = uri;
    this._finalURI = uri;
  },

  start(observer, ctxt) {
    // Do the actual operation async to give a chance for observers to add
    // themselves.
    Services.tm.dispatchToMainThread(() => {
      this._observer = observer.QueryInterface(Ci.nsIRequestObserver);
      this._ctxt = ctxt;
      this._observer.onStartRequest(this);
      let mar = getTestDirFile(FILE_SIMPLE_MAR);
      mar.copyTo(this._destination.parent, this._destination.leafName);
      let status = Cr.NS_OK;
      switch (gIncrementalDownloadErrorType++) {
        case 0:
          status = Cr.NS_ERROR_NET_RESET;
          break;
        case 1:
          status = Cr.NS_ERROR_CONNECTION_REFUSED;
          break;
        case 2:
          status = Cr.NS_ERROR_NET_RESET;
          break;
        case 3:
          status = Cr.NS_OK;
          break;
        case 4:
          status = Cr.NS_ERROR_OFFLINE;
          // After we report offline, we want to eventually show offline
          // status being changed to online.
          Services.tm.dispatchToMainThread(function () {
            Services.obs.notifyObservers(
              gAUS,
              "network:offline-status-changed",
              "online"
            );
          });
          break;
      }
      this._observer.onStopRequest(this, status);
    });
  },

  get URI() {
    return this._URI;
  },

  get currentSize() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },

  get destination() {
    return this._destination;
  },

  get finalURI() {
    return this._finalURI;
  },

  get totalSize() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },

  /* nsIRequest */
  cancel(_aStatus) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  suspend() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  isPending() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  _loadFlags: 0,
  get loadFlags() {
    return this._loadFlags;
  },
  set loadFlags(val) {
    this._loadFlags = val;
  },

  _loadGroup: null,
  get loadGroup() {
    return this._loadGroup;
  },
  set loadGroup(val) {
    this._loadGroup = val;
  },

  _name: "",
  get name() {
    return this._name;
  },

  _status: 0,
  get status() {
    return this._status;
  },
  QueryInterface: ChromeUtils.generateQI(["nsIIncrementalDownload"]),
};

/**
 * Sets the environment that will be used by the application process when it is
 * launched.
 */
function setEnvironment() {
  if (AppConstants.platform == "win") {
    // The tests use nsIProcess to launch the updater and it is simpler to just
    // set an environment variable and have the test updater set the current
    // working directory than it is to set the current working directory in the
    // test itself.
    Services.env.set("CURWORKDIRPATH", getApplyDirFile().path);
  }

  // Prevent setting the environment more than once.
  if (gShouldResetEnv !== undefined) {
    return;
  }

  gShouldResetEnv = true;

  gAddedEnvXRENoWindowsCrashDialog = false;
  gCrashReporterDisabled = null;
  gEnvXPCOMDebugBreak = null;
  gEnvXPCOMMemLeakLog = null;

  if (
    AppConstants.platform == "win" &&
    !Services.env.exists("XRE_NO_WINDOWS_CRASH_DIALOG")
  ) {
    gAddedEnvXRENoWindowsCrashDialog = true;
    debugDump(
      "setting the XRE_NO_WINDOWS_CRASH_DIALOG environment " +
        "variable to 1... previously it didn't exist"
    );
    Services.env.set("XRE_NO_WINDOWS_CRASH_DIALOG", "1");
  }

  if (Services.env.exists("XPCOM_MEM_LEAK_LOG")) {
    gEnvXPCOMMemLeakLog = Services.env.get("XPCOM_MEM_LEAK_LOG");
    debugDump(
      "removing the XPCOM_MEM_LEAK_LOG environment variable... " +
        "previous value " +
        gEnvXPCOMMemLeakLog
    );
    Services.env.set("XPCOM_MEM_LEAK_LOG", "");
  }

  if (Services.env.exists("XPCOM_DEBUG_BREAK")) {
    gEnvXPCOMDebugBreak = Services.env.get("XPCOM_DEBUG_BREAK");
    debugDump(
      "setting the XPCOM_DEBUG_BREAK environment variable to " +
        "warn... previous value " +
        gEnvXPCOMDebugBreak
    );
  } else {
    debugDump(
      "setting the XPCOM_DEBUG_BREAK environment variable to " +
        "warn... previously it didn't exist"
    );
  }
  Services.env.set("XPCOM_DEBUG_BREAK", "warn");

  if (Services.env.exists("MOZ_CRASHREPORTER_DISABLE")) {
    gCrashReporterDisabled = Services.env.get("MOZ_CRASHREPORTER_DISABLE");
    debugDump(
      "setting the MOZ_CRASHREPORTER_DISABLE environment variable to " +
        "true... previous value " +
        gCrashReporterDisabled
    );
  } else {
    debugDump(
      "setting the MOZ_CRASHREPORTER_DISABLE environment variable to " +
        "true... previously it didn't exist"
    );
  }
  Services.env.set("MOZ_CRASHREPORTER_DISABLE", "true");

  if (gEnvForceServiceFallback) {
    // This env variable forces the updater to use the service in an invalid
    // way, so that it has to fall back to updating without the service.
    debugDump("setting MOZ_FORCE_SERVICE_FALLBACK environment variable to 1");
    Services.env.set("MOZ_FORCE_SERVICE_FALLBACK", "1");
  } else if (gIsServiceTest) {
    debugDump("setting MOZ_NO_SERVICE_FALLBACK environment variable to 1");
    Services.env.set("MOZ_NO_SERVICE_FALLBACK", "1");
  }
}

/**
 * Sets the environment back to the original values after launching the
 * application.
 */
function resetEnvironment() {
  // Prevent resetting the environment more than once.
  if (gShouldResetEnv !== true) {
    return;
  }

  gShouldResetEnv = false;

  if (gEnvXPCOMMemLeakLog) {
    debugDump(
      "setting the XPCOM_MEM_LEAK_LOG environment variable back to " +
        gEnvXPCOMMemLeakLog
    );
    Services.env.set("XPCOM_MEM_LEAK_LOG", gEnvXPCOMMemLeakLog);
  }

  if (gEnvXPCOMDebugBreak) {
    debugDump(
      "setting the XPCOM_DEBUG_BREAK environment variable back to " +
        gEnvXPCOMDebugBreak
    );
    Services.env.set("XPCOM_DEBUG_BREAK", gEnvXPCOMDebugBreak);
  } else if (Services.env.exists("XPCOM_DEBUG_BREAK")) {
    debugDump("clearing the XPCOM_DEBUG_BREAK environment variable");
    Services.env.set("XPCOM_DEBUG_BREAK", "");
  }

  if (AppConstants.platform == "win" && gAddedEnvXRENoWindowsCrashDialog) {
    debugDump("removing the XRE_NO_WINDOWS_CRASH_DIALOG environment variable");
    Services.env.set("XRE_NO_WINDOWS_CRASH_DIALOG", "");
  }

  if (gEnvForceServiceFallback) {
    debugDump("removing MOZ_FORCE_SERVICE_FALLBACK environment variable");
    Services.env.set("MOZ_FORCE_SERVICE_FALLBACK", "");
  } else if (gIsServiceTest) {
    debugDump("removing MOZ_NO_SERVICE_FALLBACK environment variable");
    Services.env.set("MOZ_NO_SERVICE_FALLBACK", "");
  }
}

/**
 * `gTestFiles` needs to be set such that it contains the Update Settings file
 * before this function is called.
 */
function setUpdateSettingsUseWrongChannel() {
  if (AppConstants.platform == "macosx") {
    let replacementUpdateSettings = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
    replacementUpdateSettings = replacementUpdateSettings.parent;
    replacementUpdateSettings.append("UpdateSettings-WrongChannel");

    const updateSettings = getTestFileByName(FILE_UPDATE_SETTINGS_FRAMEWORK);
    if (!updateSettings) {
      throw new Error(
        "gTestFiles does not contain the update settings framework"
      );
    }
    updateSettings.existingFile = false;
    updateSettings.originalContents = readFileBytes(replacementUpdateSettings);
  } else {
    const updateSettings = getTestFileByName(FILE_UPDATE_SETTINGS_INI);
    if (!updateSettings) {
      throw new Error("gTestFiles does not contain the update settings INI");
    }
    updateSettings.originalContents = UPDATE_SETTINGS_CONTENTS.replace(
      "xpcshell-test",
      "wrong-channel"
    );
  }
}

class DownloadHeadersTest {
  // Collect requests to inspect header and query parameters.
  #requests = [];

  get updateUrl() {
    return `${gURLData}&appVersion=999000.0`;
  }

  async #downloadUpdate() {
    let downloadFinishedPromise = waitForEvent("update-downloaded");

    let updateCheckStarted = await gAUS.checkForBackgroundUpdates();
    Assert.ok(updateCheckStarted, "Update check should have started");

    await downloadFinishedPromise;
    // Wait an extra tick after the download has finished. If we try to check for
    // another update exactly when "update-downloaded" fires,
    // Downloader:onStopRequest won't have finished yet, which it normally would
    // have.
    await TestUtils.waitForTick();
  }

  startUpdateServer() {
    startSjsServer({
      onResponse: connection => {
        if (connection.request.method.toUpperCase() !== "HEAD") {
          // Windows BITS sends HEAD requests.  Ignore them.
          this.#requests.push(connection.request);
        }
      },
    });
  }

  /**
   * Run a single test verifying request headers.
   *
   * @param   useBits
   *          Whether to use BITS.
   * @param   backgroundTaskName
   *          Optional background task name string to set.
   * @param   userAgentPattern
   *          Regular expression that matches each request's "User-Agent"
   *          header.
   * @param   expectedExtras
   *          List of `{ mode, name }` objects that specify each request's extra
   *          headers and query parameters.  Generally, two requests are
   *          expected: the first is the Balrog query and the second is the MAR
   *          download.
   */
  async test({
    useBits,
    backgroundTaskName,
    userAgentPattern,
    expectedExtras,
  } = {}) {
    // Start fresh.
    this.#requests = [];
    reloadUpdateManagerData(true);
    removeUpdateFiles(true);

    // Configure test details.
    await UpdateUtils.setAppUpdateAutoEnabled(true);

    Services.prefs.setBoolPref(PREF_APP_UPDATE_BITS_ENABLED, !!useBits);
    // Allow the update service to download in a background task when not using BITS.
    Services.prefs.setBoolPref(
      PREF_APP_UPDATE_BACKGROUND_ALLOWDOWNLOADSWITHOUTBITS,
      !useBits && backgroundTaskName
    );

    const bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    // Pretend that this is a background task (or not, when null).
    bts.overrideBackgroundTaskNameForTesting(backgroundTaskName);

    // Make UpdateService use the changed setting.
    const { testResetIsBackgroundTaskMode } = ChromeUtils.importESModule(
      "resource://gre/modules/UpdateService.sys.mjs"
    );
    testResetIsBackgroundTaskMode();

    setUpdateURL(this.updateUrl);

    // For simplicity during testing: no staging.  N.b.: after setting
    // update URL, to avoid triggering an update too early.
    Services.prefs.setBoolPref(PREF_APP_UPDATE_DISABLEDFORTESTING, false);
    Services.prefs.setBoolPref(PREF_APP_UPDATE_STAGING_ENABLED, false);

    await this.#downloadUpdate();

    // Verify background task headers are set as expected.
    Assert.deepEqual(
      this.#requests.map(r => ({
        mode: r.hasHeader("x-backgroundtaskmode")
          ? r.getHeader("x-backgroundtaskmode")
          : null,
        name: r.hasHeader("x-backgroundtaskname")
          ? r.getHeader("x-backgroundtaskname")
          : null,
      })),
      expectedExtras,
      "Headers should be as expected"
    );

    // Verify background task query parameters are set as expected.
    Assert.deepEqual(
      this.#requests.map(r => {
        let params = new URLSearchParams(r.queryString);
        return {
          mode: params.get("backgroundTaskMode"),
          name: params.get("backgroundTaskName"),
        };
      }),
      expectedExtras,
      "Query parameters should be as expected"
    );

    // Verify that requests that identify background task mode come from the
    // expected User Agent.
    for (let r of this.#requests) {
      if (r.hasHeader("x-backgroundtaskmode")) {
        Assert.stringMatches(r.getHeader("user-agent"), userAgentPattern);
      }
    }
  }
}

class TestUpdateMutexInProcess {
  #updateMutex;
  kind;

  constructor() {
    this.#updateMutex = Cc[
      "@mozilla.org/updates/update-mutex;1"
    ].createInstance(Ci.nsIUpdateMutex);
    this.kind = "in-process";
  }

  async expectAcquire() {
    return this.#updateMutex.tryLock();
  }

  async expectFailToAcquire() {
    let failedToAcquire = !this.#updateMutex.tryLock();
    if (!failedToAcquire) {
      this.#updateMutex.unlock();
    }
    return failedToAcquire;
  }

  async release() {
    return this.#updateMutex.unlock();
  }
}

class TestUpdateMutexCrossProcess {
  static EXIT_CODE = {
    ...EXIT_CODE_BASE,
    FAILED_TO_ACQUIRE_UPDATE_MUTEX: EXIT_CODE_BASE.LAST_RESERVED + 1,
  };

  static isSuccessExitCode(aExitCode) {
    return aExitCode === TestUpdateMutexCrossProcess.EXIT_CODE.SUCCESS;
  }

  static isAcquisitionFailureExitCode(aExitCode) {
    return (
      aExitCode ===
      TestUpdateMutexCrossProcess.EXIT_CODE.FAILED_TO_ACQUIRE_UPDATE_MUTEX
    );
  }

  static isExpectedExitCode(aExitCode) {
    return (
      TestUpdateMutexCrossProcess.isSuccessExitCode(aExitCode) ||
      TestUpdateMutexCrossProcess.isAcquisitionFailureExitCode(aExitCode)
    );
  }

  #proc;
  kind;

  constructor() {
    this.#proc = null;
    this.kind = "cross-process";
  }

  runUpdateMutexTestChild() {
    return runInSubprocessWithPrelude(
      getTestDirFile("updateMutexTestChild.js").path,
      `
const EXIT_CODE = ${JSON.stringify(TestUpdateMutexCrossProcess.EXIT_CODE)};
`
    );
  }

  async expectAcquire() {
    // Use an in-process update mutex to detect the moment when the child
    // process acquires the update mutex.
    let updateMutex = Cc["@mozilla.org/updates/update-mutex;1"].createInstance(
      Ci.nsIUpdateMutex
    );

    let childIsRunning = () =>
      this.#proc !== null && this.#proc.exitCode === null;
    let updateMutexCanBeAcquired = () => {
      let isAcquired = updateMutex.tryLock();
      if (isAcquired) {
        updateMutex.unlock();
      }
      return isAcquired;
    };

    Assert.ok(
      !childIsRunning(),
      "child process for the " +
        this.kind +
        " update mutex should not be running yet"
    );
    Assert.ok(
      updateMutexCanBeAcquired(),
      "it should be possible to acquire the in-process equivalent of the " +
        this.kind +
        " update mutex before the child process is running"
    );

    this.#proc = await this.runUpdateMutexTestChild();

    // It will take the new xpcshell a little time to start up, but we should
    // see the effect on the update mutex within at most a few seconds. Our
    // active checking with updateMutexCanBeAcquired() is inherently racy, so
    // it can intermitently cause the child process to fail to acquire to
    // update mutex. We can adjust the interval for checks to lower the
    // probability that this can happen.
    await TestUtils.waitForCondition(
      () => !childIsRunning() || !updateMutexCanBeAcquired(),
      "waiting for child process to take the " +
        this.kind +
        " update mutex or exit",
      /* interval */ 1000,
      /* maxTries */ 10
    );

    // If the child is not running, it can't be holding the update mutex.
    if (!childIsRunning()) {
      Assert.ok(
        TestUpdateMutexCrossProcess.isExpectedExitCode(this.#proc.exitCode),
        "child process for the " +
          this.kind +
          " update mutex should have exited with an expected code (got: " +
          this.#proc.exitCode +
          ")"
      );

      Assert.ok(
        !TestUpdateMutexCrossProcess.isSuccessExitCode(this.#proc.exitCode),
        "child process for the " +
          this.kind +
          " should not exit normally if it failed to acquire the update mutex"
      );

      // If this is a normal failure to acquire the update mutex, just let the
      // caller deal with the failure.
      return false;
    }

    return true;
  }

  async expectFailToAcquire() {
    let proc = await this.runUpdateMutexTestChild();

    let { exitCode } = await proc.wait();
    Assert.ok(
      TestUpdateMutexCrossProcess.isExpectedExitCode(exitCode),
      "child process for the " +
        this.kind +
        " update mutex should have exited with an expected code (got: " +
        exitCode +
        ")"
    );

    let failedToAcquire =
      TestUpdateMutexCrossProcess.isAcquisitionFailureExitCode(exitCode);
    return failedToAcquire;
  }

  async release() {
    await this.#proc.kill();
    this.#proc = null;
  }
}

/**
 * Checks for updates and waits for the update to download.
 *
 * By default, this downloads an update much as `AppUpdater` would: by
 * instantiating and update checker object and then calling `gAUS.selectUpdate`
 * and `gAUS.downloadUpdate`. If `checkWithAUS` is specified, we instead do more
 * of a background update check would do and use
 * `gAUS.checkForBackgroundUpdates`.
 *
 * @param  options
 *         An optional object can be specified with these properties:
 *           appUpdateAuto
 *             Defaults to `true`. If `false`, this exercises the flow for
 *             downloading with automatic update disabled and asserts that we
 *             got a `show-prompt` update notification signal.
 *           checkWithAUS
 *             If `true`, we will check for updates via the application update
 *             service (like background update would) rather than by
 *             instantiating an update checker (like AppUpdater would), which is
 *             the default.
 *           expectDownloadRestriction
 *             If `true`, this function expects that we'll hit a download
 *             restriction rather than successfully completing the download.
 *           expectedCheckResult
 *             If specified, this should be an object with either or both keys
 *             `updateCount` and `url`, which will be checked asserted to be the
 *             values returned by the update check.
 *           expectedDownloadResult
 *             This function asserts that the download should finish with this
 *             result. Defaults to `NS_OK`.
 *           incrementalDownloadErrorType
 *             This can be used to specify an alternate value of
 *             `gIncrementalDownloadErrorType`. The default value is `3`, which
 *             corresponds to `NS_OK`.
 *           onDownloadStartCallback
 *             If provided, this callback will be invoked once during the update
 *             download, specifically when `onStartRequest` is fired. Note that
 *             in order to use this feature, `slowDownload` must be specified.
 *           slowDownload
 *             Set this to `true` to indicate that the update URL specified
 *             `useSlowDownloadMar=1&useFirstByteEarly=1`. In this case, this
 *             function will call `continueFileHandler(CONTINUE_DOWNLOAD)` in
 *             order to trigger that the update download should proceed.
 *           updateProps
 *             An object containing non default test values for an nsIUpdate.
 */
async function downloadUpdate({
  appUpdateAuto = true,
  checkWithAUS,
  expectDownloadRestriction,
  expectedCheckResult,
  expectedDownloadResult = Cr.NS_OK,
  incrementalDownloadErrorType = 3,
  onDownloadStartCallback,
  slowDownload,
  updateProps = {},
} = {}) {
  let downloadFinishedPromise;
  if (expectDownloadRestriction) {
    downloadFinishedPromise = new Promise(resolve => {
      let downloadRestrictionHitListener = (subject, topic) => {
        Services.obs.removeObserver(downloadRestrictionHitListener, topic);
        resolve();
      };
      Services.obs.addObserver(
        downloadRestrictionHitListener,
        "update-download-restriction-hit"
      );
    });
  } else {
    downloadFinishedPromise = new Promise(resolve =>
      gAUS.addDownloadListener({
        onStartRequest: _aRequest => {},
        onProgress: (_aRequest, _aContext, _aProgress, _aMaxProgress) => {},
        onStatus: (_aRequest, _aStatus, _aStatusText) => {},
        onStopRequest(request, status) {
          gAUS.removeDownloadListener(this);
          resolve({ status });
        },
        QueryInterface: ChromeUtils.generateQI([
          "nsIRequestObserver",
          "nsIProgressEventSink",
        ]),
      })
    );
  }

  let updateAvailablePromise;
  if (!appUpdateAuto) {
    updateAvailablePromise = new Promise(resolve => {
      let observer = (subject, topic, status) => {
        Services.obs.removeObserver(observer, "update-available");
        subject.QueryInterface(Ci.nsIUpdate);
        resolve({ update: subject, status });
      };
      Services.obs.addObserver(observer, "update-available");
    });
  }

  let waitToStartPromise;
  if (onDownloadStartCallback) {
    waitToStartPromise = new Promise(resolve => {
      let listener = {
        onStartRequest: async _aRequest => {
          gAUS.removeDownloadListener(listener);
          await onDownloadStartCallback();
          resolve();
        },
        onProgress: (_aRequest, _aContext, _aProgress, _aMaxProgress) => {},
        onStatus: (_aRequest, _aStatus, _aStatusText) => {},
        onStopRequest(_request, _status) {},
        QueryInterface: ChromeUtils.generateQI([
          "nsIRequestObserver",
          "nsIProgressEventSink",
        ]),
      };
      gAUS.addDownloadListener(listener);
    });
  }

  let update;
  if (checkWithAUS) {
    const updateCheckStarted = await gAUS.checkForBackgroundUpdates();
    Assert.ok(updateCheckStarted, "Update check should have started");
  } else {
    const patches = getRemotePatchString({});
    const updateString = getRemoteUpdateString(updateProps, patches);
    gResponseBody = getRemoteUpdatesXMLString(updateString);

    const { updates } = await waitForUpdateCheck(true, expectedCheckResult);

    initMockIncrementalDownload();
    gIncrementalDownloadErrorType = incrementalDownloadErrorType;

    update = await gAUS.selectUpdate(updates);
  }

  if (!appUpdateAuto) {
    const result = await updateAvailablePromise;
    Assert.equal(
      result.status,
      "show-prompt",
      "Should attempt to show the update-available prompt"
    );
    update = result.update;
  }

  // The only case where we don't call `downloadUpdate` is the
  // `checkWithAUS && appUpdateAuto` case. If we are checking like `AppUpdater`
  // would, that is just the next step in the download process. If we are
  // checking via an AUS background update without automatic updates enabled,
  // `downloadUpdate` is what we call in `UpdateListener` to signal that the
  // user has given permission to update.
  if (!checkWithAUS || !appUpdateAuto) {
    const result = await gAUS.downloadUpdate(update);
    Assert.equal(
      result,
      Ci.nsIApplicationUpdateService.DOWNLOAD_SUCCESS,
      "nsIApplicationUpdateService:downloadUpdate should succeed"
    );
  }

  if (waitToStartPromise) {
    logTestInfo("Waiting for the download to start");
    await waitToStartPromise;
    logTestInfo("Download started");
  }

  if (slowDownload) {
    await continueFileHandler(CONTINUE_DOWNLOAD);
  }

  logTestInfo("Waiting for the download to finish");
  const result = await downloadFinishedPromise;

  if (!expectDownloadRestriction) {
    Assert.equal(
      result.status,
      expectedDownloadResult,
      "The download should have the expected status"
    );

    // Wait an extra tick after the download has finished. If we try to check for
    // another update exactly when our `onStopRequest` callback fires,
    // `Downloader:onStopRequest` won't have finished yet and this function
    // ought to resolve only after the entire download process has completed.
    await TestUtils.waitForTick();
  }
}
