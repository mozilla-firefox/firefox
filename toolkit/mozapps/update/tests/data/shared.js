/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Shared code for xpcshell, mochitests-chrome, and mochitest-browser-chrome. */

// Definitions needed to run eslint on this file.
/* global AppConstants, DATA_URI_SPEC, LOG_FUNCTION */
/* global Services, URL_HOST, TestUtils */

const { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs"
);
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
  ctypes: "resource://gre/modules/ctypes.sys.mjs",
  TelemetryArchiveTesting:
    "resource://testing-common/TelemetryArchiveTesting.sys.mjs",
});

const PREF_APP_UPDATE_AUTO = "app.update.auto";
const PREF_APP_UPDATE_BACKGROUND_ALLOWDOWNLOADSWITHOUTBITS =
  "app.update.background.allowDownloadsWithoutBITS";
const PREF_APP_UPDATE_BACKGROUNDERRORS = "app.update.backgroundErrors";
const PREF_APP_UPDATE_BACKGROUNDMAXERRORS = "app.update.backgroundMaxErrors";
const PREF_APP_UPDATE_BADGEWAITTIME = "app.update.badgeWaitTime";
const PREF_APP_UPDATE_BITS_ENABLED = "app.update.BITS.enabled";
const PREF_APP_UPDATE_CANCELATIONS = "app.update.cancelations";
const PREF_APP_UPDATE_CHANNEL = "app.update.channel";
const PREF_APP_UPDATE_DOWNLOAD_MAXATTEMPTS = "app.update.download.maxAttempts";
const PREF_APP_UPDATE_DOWNLOAD_ATTEMPTS = "app.update.download.attempts";
const PREF_APP_UPDATE_DISABLEDFORTESTING = "app.update.disabledForTesting";
const PREF_APP_UPDATE_INTERVAL = "app.update.interval";
const PREF_APP_UPDATE_LASTUPDATETIME =
  "app.update.lastUpdateTime.background-update-timer";
const PREF_APP_UPDATE_LOG = "app.update.log";
const PREF_APP_UPDATE_NOTIFYDURINGDOWNLOAD = "app.update.notifyDuringDownload";
const PREF_APP_UPDATE_PROMPTWAITTIME = "app.update.promptWaitTime";
const PREF_APP_UPDATE_RETRYTIMEOUT = "app.update.socket.retryTimeout";
const PREF_APP_UPDATE_SERVICE_ENABLED = "app.update.service.enabled";
const PREF_APP_UPDATE_SOCKET_MAXERRORS = "app.update.socket.maxErrors";
const PREF_APP_UPDATE_STAGING_ENABLED = "app.update.staging.enabled";
const PREF_APP_UPDATE_UNSUPPORTED_URL = "app.update.unsupported.url";
const PREF_APP_UPDATE_URL_DETAILS = "app.update.url.details";
const PREF_APP_UPDATE_URL_MANUAL = "app.update.url.manual";
const PREF_APP_UPDATE_LANGPACK_ENABLED = "app.update.langpack.enabled";

const PREFBRANCH_APP_PARTNER = "app.partner.";
const PREF_DISTRIBUTION_ID = "distribution.id";
const PREF_DISTRIBUTION_VERSION = "distribution.version";

const CONFIG_APP_UPDATE_AUTO = "app.update.auto";

const NS_APP_PROFILE_DIR_STARTUP = "ProfDS";
const NS_APP_USER_PROFILE_50_DIR = "ProfD";
const NS_GRE_BIN_DIR = "GreBinD";
const NS_GRE_DIR = "GreD";
const NS_XPCOM_CURRENT_PROCESS_DIR = "XCurProcD";
const XRE_EXECUTABLE_FILE = "XREExeF";
const XRE_OLD_UPDATE_ROOT_DIR = "OldUpdRootD";
const XRE_UPDATE_ROOT_DIR = "UpdRootD";

const DIR_PATCH = "0";
const DIR_TOBEDELETED = "tobedeleted";
const DIR_UPDATES = "updates";
const DIR_UPDATED =
  AppConstants.platform == "macosx" ? "Updated.app" : "updated";
const DIR_DOWNLOADING = "downloading";

const FILE_ACTIVE_UPDATE_XML = "active-update.xml";
const FILE_ACTIVE_UPDATE_XML_TMP = "active-update.xml.tmp";
const FILE_APPLICATION_INI = "application.ini";
const FILE_BACKUP_UPDATE_CONFIG_JSON = "backup-update-config.json";
const FILE_BACKUP_UPDATE_ELEVATED_LOG = "backup-update-elevated.log";
const FILE_BACKUP_UPDATE_LOG = "backup-update.log";
const FILE_CHANNEL_PREFS =
  AppConstants.platform == "macosx" ? "ChannelPrefs" : "channel-prefs.js";
const FILE_INFO_PLIST = "Info.plist";
const FILE_LAST_UPDATE_ELEVATED_LOG = "last-update-elevated.log";
const FILE_LAST_UPDATE_LOG = "last-update.log";
const FILE_PRECOMPLETE = "precomplete";
const FILE_PRECOMPLETE_BAK = "precomplete.bak";
const FILE_TEST_PROCESS_UPDATES = "test_process_updates.txt";
const FILE_UPDATE_CONFIG_JSON = "update-config.json";
const FILE_UPDATE_ELEVATED_LOG = "update-elevated.log";
const FILE_UPDATE_LOG = "update.log";
const FILE_UPDATE_MAR = "update.mar";
const FILE_UPDATE_SETTINGS_FRAMEWORK = "UpdateSettings";
const FILE_UPDATE_SETTINGS_INI = "update-settings.ini";
const FILE_UPDATE_SETTINGS_INI_BAK = "update-settings.ini.bak";
const FILE_UPDATE_STATUS = "update.status";
const FILE_UPDATE_TEST = "update.test";
const FILE_UPDATE_VERSION = "update.version";
const FILE_UPDATER_INI = "updater.ini";
const FILE_UPDATES_XML = "updates.xml";
const FILE_UPDATES_XML_TMP = "updates.xml.tmp";

const UPDATE_SETTINGS_CONTENTS =
  "[Settings]\nACCEPTED_MAR_CHANNEL_IDS=xpcshell-test\n";
const PRECOMPLETE_CONTENTS = 'rmdir "nonexistent_dir/"\n';

const DIR_APP_INFO_PLIST_FILE_CONTENTS = `<?xml version="1.0" encoding="UTF-8"?>
  <!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
  <plist version="1.0">
    <dict>
      <key>CFBundleDevelopmentRegion</key><string>English</string>
      <key>CFBundleDisplayName</key><string>dir</string>
      <key>CFBundleExecutable</key><string>${AppConstants.MOZ_APP_NAME}</string>
      <key>CFBundleIdentifier</key><string>${AppConstants.MOZ_MACBUNDLE_ID}</string>
      <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
      <key>CFBundleName</key><string>dir</string>
      <key>CFBundlePackageType</key><string>APPL</string>
      <key>CFBundleSignature</key><string>????</string>
      <key>CFBundleVersion</key><string>1.0</string>
    </dict>
  </plist>`;

const PR_RDWR = 0x04;
const PR_CREATE_FILE = 0x08;
const PR_TRUNCATE = 0x20;

var gChannel;
var gDebugTest = false;

/* import-globals-from sharedUpdateXML.js */
Services.scriptloader.loadSubScript(DATA_URI_SPEC + "sharedUpdateXML.js", this);

const PERMS_FILE = FileUtils.PERMS_FILE;
const PERMS_DIRECTORY = FileUtils.PERMS_DIRECTORY;

const MODE_WRONLY = FileUtils.MODE_WRONLY;
const MODE_CREATE = FileUtils.MODE_CREATE;
const MODE_APPEND = FileUtils.MODE_APPEND;
const MODE_TRUNCATE = FileUtils.MODE_TRUNCATE;

const URI_UPDATES_PROPERTIES =
  "chrome://mozapps/locale/update/updates.properties";
const gUpdateBundle = Services.strings.createBundle(URI_UPDATES_PROPERTIES);

ChromeUtils.defineLazyGetter(this, "gAUS", function test_gAUS() {
  return Cc["@mozilla.org/updates/update-service;1"]
    .getService(Ci.nsIApplicationUpdateService)
    .QueryInterface(Ci.nsITimerCallback)
    .QueryInterface(Ci.nsIObserver);
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gUpdateManager",
  "@mozilla.org/updates/update-manager;1",
  "nsIUpdateManager"
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gUpdateChecker",
  "@mozilla.org/updates/update-checker;1",
  "nsIUpdateChecker"
);

ChromeUtils.defineLazyGetter(this, "gDefaultPrefBranch", function test_gDPB() {
  return Services.prefs.getDefaultBranch(null);
});

ChromeUtils.defineLazyGetter(this, "gPrefRoot", function test_gPR() {
  return Services.prefs.getBranch(null);
});

/**
 * Waits for the specified topic and (optionally) status.
 *
 * @param  topic
 *         String representing the topic to wait for.
 * @param  status (optional)
 *         A string representing the status on said topic to wait for.
 * @return A promise which will resolve the first time an event occurs on the
 *         specified topic, and (optionally) with the specified status.
 */
function waitForEvent(topic, status = null) {
  return new Promise(resolve =>
    Services.obs.addObserver(
      {
        observe(subject, innerTopic, innerStatus) {
          if (!status || status == innerStatus) {
            Services.obs.removeObserver(this, topic);
            resolve(innerStatus);
          }
        },
      },
      topic
    )
  );
}

/* Triggers post-update processing */
async function testPostUpdateProcessing() {
  // All of the post update processing happens during initialization
  await gAUS.internal.init(/* force = */ true);
}

/* Initializes the update service stub */
async function initUpdateServiceStub() {
  const updateServiceStub = Cc[
    "@mozilla.org/updates/update-service-stub;1"
  ].getService(Ci.nsIApplicationUpdateServiceStub);
  await updateServiceStub.init();
}

/* Initializes the Update Service, even if it has already been initialized */
async function reInitUpdateService() {
  return gAUS.internal.init(/* force = */ true);
}

async function initUpdateService() {
  return gAUS.init();
}

/**
 * Reloads the update xml files.
 *
 * @param  skipFiles (optional)
 *         If true, the update xml files will not be read and the metadata will
 *         be reset. If false (the default), the update xml files will be read
 *         to populate the update metadata.
 */
async function reloadUpdateManagerData(skipFiles = false) {
  await gUpdateManager.internal.reload(skipFiles);
}

const observer = {
  observe(aSubject, aTopic, aData) {
    if (aTopic == "nsPref:changed" && aData == PREF_APP_UPDATE_CHANNEL) {
      let channel = gDefaultPrefBranch.getCharPref(PREF_APP_UPDATE_CHANNEL);
      if (channel != gChannel) {
        debugDump("Changing channel from " + channel + " to " + gChannel);
        gDefaultPrefBranch.setCharPref(PREF_APP_UPDATE_CHANNEL, gChannel);
      }
    }
  },
  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
};

/**
 * Sets the app.update.channel preference.
 *
 * @param  aChannel
 *         The update channel.
 */
function setUpdateChannel(aChannel) {
  gChannel = aChannel;
  debugDump(
    "setting default pref " + PREF_APP_UPDATE_CHANNEL + " to " + gChannel
  );
  gDefaultPrefBranch.unlockPref(PREF_APP_UPDATE_CHANNEL);
  gDefaultPrefBranch.setCharPref(PREF_APP_UPDATE_CHANNEL, gChannel);
  gPrefRoot.addObserver(PREF_APP_UPDATE_CHANNEL, observer);
}

/**
 * Sets the effective update url.
 *
 * @param  aURL
 *         The update url. If not specified 'URL_HOST + "/update.xml"' will be
 *         used.
 */
function setUpdateURL(aURL) {
  let url = aURL ? aURL : URL_HOST + "/update.xml";
  debugDump("setting update URL to " + url);

  // The Update URL is stored in appinfo. We can replace this process's appinfo
  // directly, but that will affect only this process. Luckily, the update URL
  // is only ever read from the update process. This means that replacing
  // Services.appinfo is sufficient and we don't need to worry about registering
  // a replacement factory or anything like that.
  let origAppInfo = Services.appinfo;
  registerCleanupFunction(() => {
    Services.appinfo = origAppInfo;
  });

  // Override the appinfo object with an object that exposes all of the same
  // properties overriding just the updateURL.
  let mockAppInfo = Object.create(origAppInfo, {
    updateURL: {
      configurable: true,
      enumerable: true,
      writable: false,
      value: url,
    },
  });

  Services.appinfo = mockAppInfo;
}

/**
 * Writes the updates specified to either the active-update.xml or the
 * updates.xml.
 *
 * @param  aContent
 *         The updates represented as a string to write to the XML file.
 * @param  isActiveUpdate
 *         If true this will write to the active-update.xml otherwise it will
 *         write to the updates.xml file.
 */
function writeUpdatesToXMLFile(aContent, aIsActiveUpdate) {
  let file = getUpdateDirFile(
    aIsActiveUpdate ? FILE_ACTIVE_UPDATE_XML : FILE_UPDATES_XML
  );
  writeFile(file, aContent);
}

/**
 * Writes the current update operation/state to a file in the patch
 * directory, indicating to the patching system that operations need
 * to be performed.
 *
 * @param  aStatus
 *         The status value to write.
 */
function writeStatusFile(aStatus) {
  let file = getUpdateDirFile(FILE_UPDATE_STATUS);
  writeFile(file, aStatus + "\n");
}

/**
 * Writes the current update version to a file in the patch directory,
 * indicating to the patching system the version of the update.
 *
 * @param  aVersion
 *         The version value to write.
 */
function writeVersionFile(aVersion) {
  let file = getUpdateDirFile(FILE_UPDATE_VERSION);
  writeFile(file, aVersion + "\n");
}

/**
 * Writes text to a file. This will replace existing text if the file exists
 * and create the file if it doesn't exist.
 *
 * @param  aFile
 *         The file to write to. Will be created if it doesn't exist.
 * @param  aText
 *         The text to write to the file. If there is existing text it will be
 *         replaced.
 */
function writeFile(aFile, aText) {
  let fos = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
    Ci.nsIFileOutputStream
  );
  if (!aFile.exists()) {
    aFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
  }
  fos.init(aFile, MODE_WRONLY | MODE_CREATE | MODE_TRUNCATE, PERMS_FILE, 0);
  fos.write(aText, aText.length);
  fos.close();
}

/**
 * Attempts to remove a file. Does not fail if the file does not exist.
 * @param  file
 *         The `nsIFile` to remove.
 */
function ensureRemoved(file) {
  try {
    file.remove(false);
  } catch (ex) {
    if (ex.result != Cr.NS_ERROR_FILE_NOT_FOUND) {
      throw ex;
    }
  }
}

/**
 * Reads the current update operation/state in the status file in the patch
 * directory including the error code if it is present.
 *
 * @return The status value.
 */
function readStatusFile() {
  let file = getUpdateDirFile(FILE_UPDATE_STATUS);
  if (!file.exists()) {
    debugDump("update status file does not exists! Path: " + file.path);
    return STATE_NONE;
  }
  return readFile(file).split("\n")[0];
}

/**
 * Reads the current update operation/state in the status file in the patch
 * directory without the error code if it is present.
 *
 * @return The state value.
 */
function readStatusState() {
  return readStatusFile().split(": ")[0];
}

/**
 * Reads the current update operation/state in the status file in the patch
 * directory with the error code.
 *
 * @return The state value.
 */
function readStatusFailedCode() {
  return readStatusFile().split(": ")[1];
}

/**
 * Reads text from a file and returns the string.
 *
 * @param  aFile
 *         The file to read from.
 * @return The string of text read from the file.
 */
function readFile(aFile) {
  let fis = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  if (!aFile.exists()) {
    return null;
  }
  // Specifying -1 for ioFlags will open the file with the default of PR_RDONLY.
  // Specifying -1 for perm will open the file with the default of 0.
  fis.init(aFile, -1, -1, Ci.nsIFileInputStream.CLOSE_ON_EOF);
  let sis = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
    Ci.nsIScriptableInputStream
  );
  sis.init(fis);
  let text = sis.read(sis.available());
  sis.close();
  return text;
}

/* Returns human readable status text from the updates.properties bundle */
function getStatusText(aErrCode) {
  return getString("check_error-" + aErrCode);
}

/* Returns a string from the updates.properties bundle */
function getString(aName) {
  try {
    return gUpdateBundle.GetStringFromName(aName);
  } catch (e) {}
  return null;
}

/**
 * Gets the file extension for an nsIFile.
 *
 * @param  aFile
 *         The file to get the file extension for.
 * @return The file extension.
 */
function getFileExtension(aFile) {
  return Services.io.newFileURI(aFile).QueryInterface(Ci.nsIURL).fileExtension;
}

/**
 * Gets the specified update file or directory.
 *
 * @param   aLogLeafName
 *          The leafName of the file or directory to get.
 * @param   aWhichDir
 *          Since we started having a separate patch directory and downloading
 *          directory, there are now files with the same name that can be in
 *          either directory. This argument is optional and defaults to the
 *          patch directory for historical reasons. But if it is specified as
 *          DIR_DOWNLOADING, this function will provide the version of the file
 *          in the downloading directory. For files that aren't in the patch
 *          directory or the downloading directory, this value is ignored.
 * @return  nsIFile for the file or directory.
 */
function getUpdateDirFile(aLeafName, aWhichDir = null) {
  let file = Services.dirsvc.get(XRE_UPDATE_ROOT_DIR, Ci.nsIFile);
  switch (aLeafName) {
    case undefined:
      return file;
    case DIR_UPDATES:
    case FILE_ACTIVE_UPDATE_XML:
    case FILE_ACTIVE_UPDATE_XML_TMP:
    case FILE_UPDATE_CONFIG_JSON:
    case FILE_BACKUP_UPDATE_CONFIG_JSON:
    case FILE_TEST_PROCESS_UPDATES:
    case FILE_UPDATE_TEST:
    case FILE_UPDATES_XML:
    case FILE_UPDATES_XML_TMP:
      file.append(aLeafName);
      return file;
    case DIR_PATCH:
    case DIR_DOWNLOADING:
    case FILE_BACKUP_UPDATE_LOG:
    case FILE_BACKUP_UPDATE_ELEVATED_LOG:
    case FILE_LAST_UPDATE_LOG:
    case FILE_LAST_UPDATE_ELEVATED_LOG:
      file.append(DIR_UPDATES);
      file.append(aLeafName);
      return file;
    case FILE_UPDATE_LOG:
    case FILE_UPDATE_ELEVATED_LOG:
    case FILE_UPDATE_MAR:
    case FILE_UPDATE_STATUS:
    case FILE_UPDATE_VERSION:
    case FILE_UPDATER_INI:
      file.append(DIR_UPDATES);
      if (aWhichDir == DIR_DOWNLOADING) {
        file.append(DIR_DOWNLOADING);
      } else {
        file.append(DIR_PATCH);
      }
      file.append(aLeafName);
      return file;
  }

  throw new Error(
    "The leafName specified is not handled by this function, " +
      "leafName: " +
      aLeafName
  );
}

/**
 * Helper function for getting the nsIFile for a file in the directory where the
 * update will be staged.
 *
 * The files for the update are located two directories below the stage
 * directory since Mac OS X sets the last modified time for the root directory
 * to the current time and if the update changes any files in the root directory
 * then it wouldn't be possible to test (bug 600098).
 *
 * @param   aRelPath (optional)
 *          The relative path to the file or directory to get from the root of
 *          the stage directory. If not specified the stage directory will be
 *          returned.
 * @return  The nsIFile for the file in the directory where the update will be
 *          staged.
 */
function getStageDirFile(aRelPath) {
  let file;
  if (AppConstants.platform == "macosx") {
    file = getUpdateDirFile(DIR_PATCH);
  } else {
    file = getGREBinDir();
  }
  file.append(DIR_UPDATED);
  if (aRelPath) {
    let pathParts = aRelPath.split("/");
    for (let i = 0; i < pathParts.length; i++) {
      if (pathParts[i]) {
        file.append(pathParts[i]);
      }
    }
  }
  return file;
}

/**
 * Removes the update files that typically need to be removed by tests without
 * removing the directories since removing the directories has caused issues
 * when running tests with --verify and recursively removes the stage directory.
 *
 * @param   aRemoveLogFiles
 *          When true the update log files will also be removed. This allows
 *          for the inspection of the log files while troubleshooting tests.
 */
function removeUpdateFiles(aRemoveLogFiles) {
  let files = [
    [FILE_ACTIVE_UPDATE_XML],
    [FILE_UPDATES_XML],
    [FILE_UPDATE_STATUS],
    [FILE_UPDATE_VERSION],
    [FILE_UPDATE_MAR],
    [FILE_UPDATE_MAR, DIR_DOWNLOADING],
    [FILE_UPDATER_INI],
  ];

  if (aRemoveLogFiles) {
    files = files.concat([
      [FILE_BACKUP_UPDATE_LOG],
      [FILE_LAST_UPDATE_LOG],
      [FILE_UPDATE_LOG],
      [FILE_BACKUP_UPDATE_ELEVATED_LOG],
      [FILE_LAST_UPDATE_ELEVATED_LOG],
      [FILE_UPDATE_ELEVATED_LOG],
    ]);
  }

  for (let i = 0; i < files.length; i++) {
    let file = getUpdateDirFile.apply(null, files[i]);
    try {
      if (file.exists()) {
        file.remove(false);
      }
    } catch (e) {
      logTestInfo(
        "Unable to remove file. Path: " + file.path + ", Exception: " + e
      );
    }
  }

  let stageDir = getStageDirFile();
  if (stageDir.exists()) {
    try {
      removeDirRecursive(stageDir);
    } catch (e) {
      logTestInfo(
        "Unable to remove directory. Path: " +
          stageDir.path +
          ", Exception: " +
          e
      );
    }
  }
}

/**
 * Deletes a directory and its children. First it tries nsIFile::Remove(true).
 * If that fails it will fall back to recursing, setting the appropriate
 * permissions, and deleting the current entry.
 *
 * @param  aDir
 *         nsIFile for the directory to be deleted.
 */
function removeDirRecursive(aDir) {
  if (!aDir.exists()) {
    return;
  }

  if (!aDir.isDirectory()) {
    throw new Error("Only a directory can be passed to this funtion!");
  }

  try {
    debugDump("attempting to remove directory. Path: " + aDir.path);
    aDir.remove(true);
    return;
  } catch (e) {
    logTestInfo("non-fatal error removing directory. Exception: " + e);
  }

  let dirEntries = aDir.directoryEntries;
  while (dirEntries.hasMoreElements()) {
    let entry = dirEntries.nextFile;

    if (entry.isDirectory()) {
      removeDirRecursive(entry);
    } else {
      entry.permissions = PERMS_FILE;
      try {
        debugDump("attempting to remove file. Path: " + entry.path);
        entry.remove(false);
      } catch (e) {
        logTestInfo("error removing file. Exception: " + e);
        throw e;
      }
    }
  }

  aDir.permissions = PERMS_DIRECTORY;
  try {
    debugDump("attempting to remove directory. Path: " + aDir.path);
    aDir.remove(true);
  } catch (e) {
    logTestInfo("error removing directory. Exception: " + e);
    throw e;
  }
}

/**
 * Returns the directory for the currently running process. This is used to
 * clean up after the tests and to locate the active-update.xml and updates.xml
 * files.
 *
 * @return nsIFile for the current process directory.
 */
function getCurrentProcessDir() {
  return Services.dirsvc.get(NS_XPCOM_CURRENT_PROCESS_DIR, Ci.nsIFile);
}

/**
 * Returns the Gecko Runtime Engine directory where files other than executable
 * binaries are located. On Mac OS X this will be <bundle>/Contents/Resources/
 * and the installation directory on all other platforms.
 *
 * @return nsIFile for the Gecko Runtime Engine directory.
 */
function getGREDir() {
  return Services.dirsvc.get(NS_GRE_DIR, Ci.nsIFile);
}

/**
 * Returns the Gecko Runtime Engine Binary directory where the executable
 * binaries are located such as the updater binary (Windows and Linux) or
 * updater package (Mac OS X). On Mac OS X this will be
 * <bundle>/Contents/MacOS/ and the installation directory on all other
 * platforms.
 *
 * @return nsIFile for the Gecko Runtime Engine Binary directory.
 */
function getGREBinDir() {
  return Services.dirsvc.get(NS_GRE_BIN_DIR, Ci.nsIFile);
}

/**
 * Synchronously writes the value of the app.update.auto setting to the update
 * configuration file on Windows or to a user preference on other platforms.
 * When the value passed to this function is null or undefined it will remove
 * the configuration file on Windows or the user preference on other platforms.
 *
 * @param  aEnabled
 *         Possible values are true, false, null, and undefined. When true or
 *         false this value will be written for app.update.auto in the update
 *         configuration file on Windows or to the user preference on other
 *         platforms. When null or undefined the update configuration file will
 *         be removed on Windows or the user preference will be removed on other
 *         platforms.
 */
function setAppUpdateAutoSync(aEnabled) {
  if (AppConstants.platform == "win") {
    let file = getUpdateDirFile(FILE_UPDATE_CONFIG_JSON);
    if (aEnabled === undefined || aEnabled === null) {
      if (file.exists()) {
        file.remove(false);
      }
    } else {
      writeFile(
        file,
        '{"' + CONFIG_APP_UPDATE_AUTO + '":' + aEnabled.toString() + "}"
      );
    }
  } else if (aEnabled === undefined || aEnabled === null) {
    if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_AUTO)) {
      Services.prefs.clearUserPref(PREF_APP_UPDATE_AUTO);
    }
  } else {
    Services.prefs.setBoolPref(PREF_APP_UPDATE_AUTO, aEnabled);
  }
}

/**
 * Logs TEST-INFO messages.
 *
 * @param  aText
 *         The text to log.
 * @param  aCaller (optional)
 *         An optional Components.stack.caller. If not specified
 *         Components.stack.caller will be used.
 */
function logTestInfo(aText, aCaller) {
  let caller = aCaller ? aCaller : Components.stack.caller;
  let now = new Date();
  let hh = now.getHours();
  let mm = now.getMinutes();
  let ss = now.getSeconds();
  let ms = now.getMilliseconds();
  let time =
    (hh < 10 ? "0" + hh : hh) +
    ":" +
    (mm < 10 ? "0" + mm : mm) +
    ":" +
    (ss < 10 ? "0" + ss : ss) +
    ":";
  if (ms < 10) {
    time += "00";
  } else if (ms < 100) {
    time += "0";
  }
  time += ms;
  let msg =
    time +
    " | TEST-INFO | " +
    caller.filename +
    " | [" +
    caller.name +
    " : " +
    caller.lineNumber +
    "] " +
    aText;
  LOG_FUNCTION(msg);
}

/**
 * Logs TEST-INFO messages when gDebugTest evaluates to true.
 *
 * @param  aText
 *         The text to log.
 * @param  aCaller (optional)
 *         An optional Components.stack.caller. If not specified
 *         Components.stack.caller will be used.
 */
function debugDump(aText, aCaller) {
  if (gDebugTest) {
    let caller = aCaller ? aCaller : Components.stack.caller;
    logTestInfo(aText, caller);
  }
}

/**
 * Creates the continue file used to signal that update staging or the mock http
 * server should continue. The delay this creates allows the tests to verify the
 * user interfaces before they auto advance to other phases of an update. The
 * continue file for staging will be deleted by the test updater and the
 * continue file for the update check and update download requests will be
 * deleted by the test http server handler implemented in app_update.sjs. The
 * test returns a promise so the test can wait on the deletion of the continue
 * file when necessary. If the continue file still exists at the end of a test
 * it will be removed to prevent it from affecting tests that run after the test
 * that created it.
 *
 * @param  leafName
 *         The leafName of the file to create. This should be one of the
 *         folowing constants that are defined in testConstants.js:
 *         CONTINUE_CHECK
 *         CONTINUE_DOWNLOAD
 *         CONTINUE_STAGING
 * @return Promise
 *         Resolves when the file is deleted or if the file is not deleted when
 *         the check for the file's existence times out. If the file isn't
 *         deleted before the check for the file's existence times out it will
 *         be deleted when the test ends so it doesn't affect tests that run
 *         after the test that created the continue file.
 * @throws If the file already exists.
 */
async function continueFileHandler(leafName) {
  // The total time to wait with 300 retries and the default interval of 100 is
  // approximately 30 seconds.
  let interval = 100;
  let retries = 300;
  let continueFile;
  if (leafName == CONTINUE_STAGING) {
    // The total time to wait with 600 retries and an interval of 200 is
    // approximately 120 seconds.
    interval = 200;
    retries = 600;
    continueFile = getGREBinDir();
    if (AppConstants.platform == "macosx") {
      continueFile = continueFile.parent.parent;
    }
    continueFile.append(leafName);
  } else {
    continueFile = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
    let continuePath = REL_PATH_DATA + leafName;
    let continuePathParts = continuePath.split("/");
    for (let i = 0; i < continuePathParts.length; ++i) {
      continueFile.append(continuePathParts[i]);
    }
  }
  if (continueFile.exists()) {
    logTestInfo(
      "The continue file should not exist, path: " + continueFile.path
    );
    continueFile.remove(false);
  }
  debugDump("Creating continue file, path: " + continueFile.path);
  continueFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
  // If for whatever reason the continue file hasn't been removed when a test
  // has finished remove it during cleanup so it doesn't affect tests that run
  // after the test that created it.
  registerCleanupFunction(() => {
    if (continueFile.exists()) {
      logTestInfo(
        "Removing continue file during test cleanup, path: " + continueFile.path
      );
      continueFile.remove(false);
    }
  });
  return TestUtils.waitForCondition(
    () => !continueFile.exists(),
    "Waiting for file to be deleted, path: " + continueFile.path,
    interval,
    retries
  ).catch(_e => {
    logTestInfo(
      "Continue file was not removed after checking " +
        retries +
        " times, path: " +
        continueFile.path
    );
  });
}

async function waitForUpdatePing(archiveChecker, expectedProperties) {
  // We cannot control when the ping will be generated/archived after we trigger
  // an update, so let's make sure to have one before moving on with validation.
  let updatePing;
  await TestUtils.waitForCondition(
    async function () {
      // Check that the ping made it into the Telemetry archive.
      // The test data is defined in ../data/sharedUpdateXML.js
      updatePing = await archiveChecker.promiseFindPing(
        "update",
        expectedProperties
      );
      return !!updatePing;
    },
    "Wait for Update Ping to be generated",
    500,
    100
  );
  return updatePing;
}

/**
 * It's frequently desirable to run a test many times with different parameters
 * each time.
 *
 * @param  testFn
 *         The function to test. It is expected to be a function that takes a
 *         single object of named parameters. For example:
 *           function test({param1, param2})
 * @param  parameters
 *         This can either be an object or an array of objects.
 *
 *         If it is an array of objects, it will be treated as a list of
 *         sets of parameters. The number of times the test is executed will be
 *         equal to the length of the array.
 *         For example:
 *           [{param1: value1}, {param1: value2}]
 *
 *         If it is an object, each key will be treated as a parameter with the
 *         corresponding value will be an array of values that parameter can
 *         have. The number of times the test is executed will be equal to the
 *         product of all the array lengths.
 *         For example:
 *            {param1: [value1, value2], {param2: [value3]}}
 * @param  options
 *         An additional object can be passed that with any of these supported
 *         options:
 *           skipFn
 *             This is evaluated with the test's parameters before each test. If
 *             it returns `true`, the test is not run with that set of
 *             parameters.
 */
async function parameterizedTest(testFn, parameters, { skipFn } = {}) {
  logTestInfo(`parameterizedTest - Testing ${testFn.name}`);

  const maybeRunTest = async params => {
    const invocationDesc = `${testFn.name}(${JSON.stringify(params)})`;
    if (skipFn && (await skipFn(params))) {
      logTestInfo("parameterizedTest - SKIPPING " + invocationDesc);
      return;
    }
    logTestInfo("parameterizedTest - START " + invocationDesc);
    await testFn(params);
    logTestInfo("parameterizedTest - COMPLETE " + invocationDesc);
  };

  if (Array.isArray(parameters)) {
    for (const params of parameters) {
      await maybeRunTest(params);
    }
  } else {
    const recurse = async (chosenParams, remainingPossibleParams) => {
      if (!remainingPossibleParams.length) {
        await maybeRunTest(chosenParams);
        return;
      }
      const [param, argValues] = remainingPossibleParams.shift();
      for (const argValue of argValues) {
        chosenParams[param] = argValue;
        // Clone parameters when we recurse.
        await recurse(Object.assign({}, chosenParams), [
          ...remainingPossibleParams,
        ]);
      }
    };
    await recurse({}, Object.entries(parameters));
  }

  logTestInfo(`parameterizedTest - Finished testing ${testFn.name}`);
}

async function startBitsMarDownload(url) {
  return gAUS.wrappedJSObject.makeBitsRequest({ url });
}

async function connectToBitsMarDownload(bitsId) {
  return gAUS.wrappedJSObject.makeBitsRequest({ bitsId });
}
