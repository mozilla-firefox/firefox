/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Shared code for xpcshell, mochitests-chrome, mochitest-browser-chrome, and
 * SJS server-side scripts for the test http server.
 */

/**
 * Helper functions for creating xml strings used by application update tests.
 */

/* import-globals-from ../browser/testConstants.js */

/* global Services, UpdateUtils, gURLData */

const FILE_SIMPLE_MAR = "simple.mar";
const SIZE_SIMPLE_MAR = "1419";

const STATE_NONE = "null";
const STATE_DOWNLOADING = "downloading";
const STATE_PENDING = "pending";
const STATE_PENDING_SVC = "pending-service";
const STATE_PENDING_ELEVATE = "pending-elevate";
const STATE_APPLYING = "applying";
const STATE_APPLIED = "applied";
const STATE_APPLIED_SVC = "applied-service";
const STATE_SUCCEEDED = "succeeded";
const STATE_DOWNLOAD_FAILED = "download-failed";
const STATE_FAILED = "failed";

const LOADSOURCE_ERROR_WRONG_SIZE = 2;
const CRC_ERROR = 4;
const READ_ERROR = 6;
const WRITE_ERROR = 7;
const READ_STRINGS_MEM_ERROR = 10;
const ARCHIVE_READER_MEM_ERROR = 11;
const BSPATCH_MEM_ERROR = 12;
const UPDATER_MEM_ERROR = 13;
const UPDATER_QUOTED_PATH_MEM_ERROR = 14;
const MAR_CHANNEL_MISMATCH_ERROR = 22;
const VERSION_DOWNGRADE_ERROR = 23;
const UPDATE_SETTINGS_FILE_CHANNEL = 38;
const UNEXPECTED_BSPATCH_ERROR = 41;
const SERVICE_COULD_NOT_COPY_UPDATER = 49;
const SERVICE_INVALID_APPLYTO_DIR_STAGED_ERROR = 52;
const SERVICE_INVALID_APPLYTO_DIR_ERROR = 54;
const SERVICE_INVALID_INSTALL_DIR_PATH_ERROR = 55;
const SERVICE_INVALID_WORKING_DIR_PATH_ERROR = 56;
const INVALID_APPLYTO_DIR_STAGED_ERROR = 72;
const INVALID_APPLYTO_DIR_ERROR = 74;
const INVALID_INSTALL_DIR_PATH_ERROR = 75;
const INVALID_WORKING_DIR_PATH_ERROR = 76;
const INVALID_CALLBACK_PATH_ERROR = 77;
const INVALID_CALLBACK_DIR_ERROR = 78;

// Error codes 80 through 99 are reserved for nsUpdateService.js and are not
// defined in common/updatererrors.h
const ERR_OLDER_VERSION_OR_SAME_BUILD = 90;
const ERR_UPDATE_STATE_NONE = 91;
const ERR_CHANNEL_CHANGE = 92;
const INVALID_UPDATER_STATUS_CODE = 99;

const BACKGROUND_TASK_SHARING_VIOLATION = 106;

const STATE_FAILED_DELIMETER = ": ";

const STATE_FAILED_LOADSOURCE_ERROR_WRONG_SIZE =
  STATE_FAILED + STATE_FAILED_DELIMETER + LOADSOURCE_ERROR_WRONG_SIZE;
const STATE_FAILED_CRC_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + CRC_ERROR;
const STATE_FAILED_READ_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + READ_ERROR;
const STATE_FAILED_WRITE_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + WRITE_ERROR;
const STATE_FAILED_READ_STRINGS_MEM_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + READ_STRINGS_MEM_ERROR;
const STATE_FAILED_ARCHIVE_READER_MEM_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + ARCHIVE_READER_MEM_ERROR;
const STATE_FAILED_BSPATCH_MEM_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + BSPATCH_MEM_ERROR;
const STATE_FAILED_UPDATER_MEM_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + UPDATER_MEM_ERROR;
const STATE_FAILED_UPDATER_QUOTED_PATH_MEM_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + UPDATER_QUOTED_PATH_MEM_ERROR;
const STATE_FAILED_MAR_CHANNEL_MISMATCH_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + MAR_CHANNEL_MISMATCH_ERROR;
const STATE_FAILED_VERSION_DOWNGRADE_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + VERSION_DOWNGRADE_ERROR;
const STATE_FAILED_UPDATE_SETTINGS_FILE_CHANNEL =
  STATE_FAILED + STATE_FAILED_DELIMETER + UPDATE_SETTINGS_FILE_CHANNEL;
const STATE_FAILED_UNEXPECTED_BSPATCH_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + UNEXPECTED_BSPATCH_ERROR;
const STATE_FAILED_SERVICE_COULD_NOT_COPY_UPDATER =
  STATE_FAILED + STATE_FAILED_DELIMETER + SERVICE_COULD_NOT_COPY_UPDATER;
const STATE_FAILED_SERVICE_INVALID_APPLYTO_DIR_STAGED_ERROR =
  STATE_FAILED +
  STATE_FAILED_DELIMETER +
  SERVICE_INVALID_APPLYTO_DIR_STAGED_ERROR;
const STATE_FAILED_SERVICE_INVALID_APPLYTO_DIR_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + SERVICE_INVALID_APPLYTO_DIR_ERROR;
const STATE_FAILED_SERVICE_INVALID_INSTALL_DIR_PATH_ERROR =
  STATE_FAILED +
  STATE_FAILED_DELIMETER +
  SERVICE_INVALID_INSTALL_DIR_PATH_ERROR;
const STATE_FAILED_SERVICE_INVALID_WORKING_DIR_PATH_ERROR =
  STATE_FAILED +
  STATE_FAILED_DELIMETER +
  SERVICE_INVALID_WORKING_DIR_PATH_ERROR;
const STATE_FAILED_INVALID_APPLYTO_DIR_STAGED_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + INVALID_APPLYTO_DIR_STAGED_ERROR;
const STATE_FAILED_INVALID_APPLYTO_DIR_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + INVALID_APPLYTO_DIR_ERROR;
const STATE_FAILED_INVALID_INSTALL_DIR_PATH_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + INVALID_INSTALL_DIR_PATH_ERROR;
const STATE_FAILED_INVALID_WORKING_DIR_PATH_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + INVALID_WORKING_DIR_PATH_ERROR;
const STATE_FAILED_INVALID_CALLBACK_PATH_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + INVALID_CALLBACK_PATH_ERROR;
const STATE_FAILED_INVALID_CALLBACK_DIR_ERROR =
  STATE_FAILED + STATE_FAILED_DELIMETER + INVALID_CALLBACK_DIR_ERROR;
const STATE_FAILED_BACKGROUND_TASK_SHARING_VIOLATION =
  STATE_FAILED + STATE_FAILED_DELIMETER + BACKGROUND_TASK_SHARING_VIOLATION;

const DEFAULT_UPDATE_VERSION = "999999.0";

/**
 * Constructs a string representing a remote update xml file.
 *
 * @param  aUpdates
 *         The string representing the update elements.
 * @return The string representing a remote update xml file.
 */
function getRemoteUpdatesXMLString(aUpdates) {
  return '<?xml version="1.0"?><updates>' + aUpdates + "</updates>";
}

/**
 * Constructs a string representing an update element for a remote update xml
 * file. See getUpdateString for parameter information not provided below.
 *
 * @param  aUpdateProps
 *         An object containing non default test values for an nsIUpdate.
 *         See updateProps names below for possible object names.
 * @param  aPatches
 *         String representing the application update patches.
 * @return The string representing an update element for an update xml file.
 */
function getRemoteUpdateString(aUpdateProps, aPatches) {
  const updateProps = {
    appVersion: DEFAULT_UPDATE_VERSION,
    buildID: "20080811053724",
    custom1: null,
    custom2: null,
    detailsURL: URL_HTTP_UPDATE_SJS + "?uiURL=DETAILS",
    displayVersion: null,
    platformVersion: null,
    name: "App Update Test",
    promptWaitTime: null,
    type: "major",
    unsupported: false,
  };

  for (let name in aUpdateProps) {
    updateProps[name] = aUpdateProps[name];
  }

  // To test that text nodes are handled properly the string returned contains
  // spaces and newlines.
  return getUpdateString(updateProps) + ">\n " + aPatches + "\n</update>\n";
}

/**
 * Constructs a string representing a patch element for a remote update xml
 * file. See getPatchString for parameter information not provided below.
 *
 * @param  aPatchProps (optional)
 *         An object containing non default test values for an nsIUpdatePatch.
 *         See patchProps below for possible object names.
 * @return The string representing a patch element for a remote update xml file.
 */
function getRemotePatchString(aPatchProps) {
  const patchProps = {
    type: "complete",
    _url: null,
    get url() {
      if (this._url) {
        return this._url;
      }
      if (gURLData) {
        return gURLData + FILE_SIMPLE_MAR;
      }
      return null;
    },
    set url(val) {
      this._url = val;
    },
    custom1: null,
    custom2: null,
    size: SIZE_SIMPLE_MAR,
  };

  for (let name in aPatchProps) {
    patchProps[name] = aPatchProps[name];
  }

  return getPatchString(patchProps) + "/>";
}

/**
 * Constructs a string representing a local update xml file.
 *
 * @param  aUpdates
 *         The string representing the update elements.
 * @return The string representing a local update xml file.
 */
function getLocalUpdatesXMLString(aUpdates) {
  if (!aUpdates || aUpdates == "") {
    return '<updates xmlns="http://www.mozilla.org/2005/app-update"/>';
  }
  return (
    '<updates xmlns="http://www.mozilla.org/2005/app-update">' +
    aUpdates +
    "</updates>"
  );
}

/**
 * Constructs a string representing an update element for a local update xml
 * file. See getUpdateString for parameter information not provided below.
 *
 * @param  aUpdateProps
 *         An object containing non default test values for an nsIUpdate.
 *         See updateProps names below for possible object names.
 * @param  aPatches
 *         String representing the application update patches.
 * @return The string representing an update element for an update xml file.
 */
function getLocalUpdateString(aUpdateProps, aPatches) {
  const updateProps = {
    _appVersion: null,
    get appVersion() {
      if (this._appVersion) {
        return this._appVersion;
      }
      if (Services && Services.appinfo && Services.appinfo.version) {
        return Services.appinfo.version;
      }
      return DEFAULT_UPDATE_VERSION;
    },
    set appVersion(val) {
      this._appVersion = val;
    },
    buildID: "20080811053724",
    channel: UpdateUtils ? UpdateUtils.getUpdateChannel() : "default",
    custom1: null,
    custom2: null,
    detailsURL: URL_HTTP_UPDATE_SJS + "?uiURL=DETAILS",
    displayVersion: null,
    platformVersion: null,
    foregroundDownload: "true",
    installDate: "1238441400314",
    isCompleteUpdate: "true",
    name: "App Update Test",
    previousAppVersion: null,
    promptWaitTime: null,
    serviceURL: "http://test_service/",
    statusText: "Install Pending",
    type: "major",
  };

  for (let name in aUpdateProps) {
    updateProps[name] = aUpdateProps[name];
  }

  let checkInterval = updateProps.checkInterval
    ? 'checkInterval="' + updateProps.checkInterval + '" '
    : "";
  let channel = 'channel="' + updateProps.channel + '" ';
  let isCompleteUpdate =
    'isCompleteUpdate="' + updateProps.isCompleteUpdate + '" ';
  let foregroundDownload = updateProps.foregroundDownload
    ? 'foregroundDownload="' + updateProps.foregroundDownload + '" '
    : "";
  let installDate = 'installDate="' + updateProps.installDate + '" ';
  let previousAppVersion = updateProps.previousAppVersion
    ? 'previousAppVersion="' + updateProps.previousAppVersion + '" '
    : "";
  let statusText = updateProps.statusText
    ? 'statusText="' + updateProps.statusText + '" '
    : "";
  let serviceURL = 'serviceURL="' + updateProps.serviceURL + '">';

  return (
    getUpdateString(updateProps) +
    " " +
    checkInterval +
    channel +
    isCompleteUpdate +
    foregroundDownload +
    installDate +
    previousAppVersion +
    statusText +
    serviceURL +
    aPatches +
    "</update>"
  );
}

/**
 * Constructs a string representing a patch element for a local update xml file.
 * See getPatchString for parameter information not provided below.
 *
 * @param  aPatchProps (optional)
 *         An object containing non default test values for an nsIUpdatePatch.
 *         See patchProps below for possible object names.
 * @return The string representing a patch element for a local update xml file.
 */
function getLocalPatchString(aPatchProps) {
  const patchProps = {
    type: "complete",
    url: gURLData + FILE_SIMPLE_MAR,
    size: SIZE_SIMPLE_MAR,
    custom1: null,
    custom2: null,
    selected: "true",
    state: STATE_SUCCEEDED,
    bitsId: null,
  };

  for (let name in aPatchProps) {
    patchProps[name] = aPatchProps[name];
  }

  let patchString = getPatchString(patchProps);
  patchString += ' selected="' + patchProps.selected + '"';
  patchString += ' state="' + patchProps.state + '"';
  if (patchProps.bitsId) {
    patchString += ' bitsId="' + patchProps.bitsId + '"';
  }
  patchString += "/>";
  return patchString;
}

/**
 * Constructs a string representing an update element for a remote update xml
 * file.
 *
 * @param  aUpdateProps (optional)
 *         An object containing non default test values for an nsIUpdate.
 *         See the aUpdateProps property names below for possible object names.
 * @return The string representing an update element for an update xml file.
 */
function getUpdateString(aUpdateProps) {
  let type = 'type="' + aUpdateProps.type + '" ';
  let name = 'name="' + aUpdateProps.name + '" ';
  let displayVersion = aUpdateProps.displayVersion
    ? 'displayVersion="' + aUpdateProps.displayVersion + '" '
    : "";
  let appVersion = 'appVersion="' + aUpdateProps.appVersion + '" ';
  let platformVersion =
    'platformVersion="' + aUpdateProps.platformVersion + '" ';
  // Not specifying a detailsURL will cause a leak due to bug 470244
  let detailsURL = 'detailsURL="' + aUpdateProps.detailsURL + '" ';
  let promptWaitTime = aUpdateProps.promptWaitTime
    ? 'promptWaitTime="' + aUpdateProps.promptWaitTime + '" '
    : "";
  let disableBITS = aUpdateProps.disableBITS
    ? 'disableBITS="' + aUpdateProps.disableBITS + '" '
    : "";
  let disableBackgroundUpdates = aUpdateProps.disableBackgroundUpdates
    ? 'disableBackgroundUpdates="' +
      aUpdateProps.disableBackgroundUpdates +
      '" '
    : "";
  let unsupported = aUpdateProps.unsupported ? 'unsupported="true" ' : "";
  let custom1 = aUpdateProps.custom1 ? aUpdateProps.custom1 + " " : "";
  let custom2 = aUpdateProps.custom2 ? aUpdateProps.custom2 + " " : "";
  let buildID = 'buildID="' + aUpdateProps.buildID + '"';

  return (
    "<update " +
    type +
    name +
    displayVersion +
    appVersion +
    platformVersion +
    detailsURL +
    promptWaitTime +
    disableBITS +
    disableBackgroundUpdates +
    unsupported +
    custom1 +
    custom2 +
    buildID
  );
}

/**
 * Constructs a string representing a patch element for an update xml file.
 *
 * @param  aPatchProps (optional)
 *         An object containing non default test values for an nsIUpdatePatch.
 *         See the patchProps property names below for possible object names.
 * @return The string representing a patch element for an update xml file.
 */
function getPatchString(aPatchProps) {
  let type = 'type="' + aPatchProps.type + '" ';
  let url = 'URL="' + aPatchProps.url + '" ';
  let size = 'size="' + aPatchProps.size + '"';
  let custom1 = aPatchProps.custom1 ? aPatchProps.custom1 + " " : "";
  let custom2 = aPatchProps.custom2 ? aPatchProps.custom2 + " " : "";
  return "<patch " + type + url + custom1 + custom2 + size;
}

/**
 * Reads the binary contents of a file and returns it as a string.
 *
 * @param  aFile
 *         The file to read from.
 * @return The contents of the file as a string.
 */
function readFileBytes(aFile) {
  let fis = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  // Specifying -1 for ioFlags will open the file with the default of PR_RDONLY.
  // Specifying -1 for perm will open the file with the default of 0.
  fis.init(aFile, -1, -1, Ci.nsIFileInputStream.CLOSE_ON_EOF);
  let bis = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  bis.setInputStream(fis);
  let data = [];
  let count = fis.available();
  while (count > 0) {
    let bytes = bis.readByteArray(Math.min(65535, count));
    data.push(String.fromCharCode.apply(null, bytes));
    count -= bytes.length;
    if (!bytes.length) {
      throw new Error("Nothing read from input stream!");
    }
  }
  data = data.join("");
  fis.close();
  return data.toString();
}
