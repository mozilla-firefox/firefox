/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

import {
  BitsError,
  BitsUnknownError,
} from "moz-src:///toolkit/components/bitsdownload/Bits.sys.mjs";

const BITS_CODE_TYPE_NAMES = new Map([
  [Ci.nsIBits.ERROR_CODE_TYPE_NONE, "none"],
  [Ci.nsIBits.ERROR_CODE_TYPE_NSRESULT, "nsresult"],
  [Ci.nsIBits.ERROR_CODE_TYPE_HRESULT, "hresult"],
  [Ci.nsIBits.ERROR_CODE_TYPE_STRING, "string"],
  [Ci.nsIBits.ERROR_CODE_TYPE_EXCEPTION, "exception"],
]);

export var AUSTLMY = {
  // Telemetry for the application update background update check occurs when
  // the background update timer fires after the update interval which is
  // determined by the app.update.interval preference and its glean metric IDs
  // have the suffix 'Notify'.
  // Telemetry for the externally initiated background update check occurs when
  // a call is made to |checkForBackgroundUpdates| which is typically initiated
  // by an application when it has determined that the application should have
  // received an update. This has separate telemetry so it is possible to
  // analyze using the telemetry data systems that have not been updating when
  // they should have.

  // The update check was performed by the call to checkForBackgroundUpdates in
  // nsUpdateService.js.
  EXTERNAL: "External",
  // The update check was performed by the call to notify in nsUpdateService.js.
  NOTIFY: "Notify",
  // The update check was performed after an update is already ready. There is
  // currently no way for a user to initiate an update check when there is a
  // ready update (the UI just prompts you to install the ready update). So
  // subsequent update checks are necessarily "notify" update checks, not
  // "external" ones.
  SUBSEQUENT: "Subsequent",

  /**
   * Values for the Glean.update.checkCodeNotify and
   * Glean.update.checkCodeExternal custom_distribution metrics.
   */
  // No update found (no notification)
  CHK_NO_UPDATE_FOUND: 0,
  // Update will be downloaded in the background (background download)
  CHK_DOWNLOAD_UPDATE: 1,
  // Showing prompt due to preference (update notification)
  CHK_SHOWPROMPT_PREF: 3,
  // Already has an active update in progress (no notification)
  CHK_HAS_ACTIVEUPDATE: 8,
  // A background download is already in progress (no notification)
  CHK_IS_DOWNLOADING: 9,
  // An update is already staged (no notification)
  CHK_IS_STAGED: 10,
  // An update is already downloaded (no notification)
  CHK_IS_DOWNLOADED: 11,
  // Note: codes 12-13 were removed along with the |app.update.enabled| pref.
  // Unable to check for updates per hasUpdateMutex() (no notification)
  CHK_NO_MUTEX: 14,
  // Unable to check for updates per gCanCheckForUpdates (no notification). This
  // should be covered by other codes and is recorded just in case.
  CHK_UNABLE_TO_CHECK: 15,
  // Note: code 16 was removed when the feature for disabling updates for the
  // session was removed.
  // Unable to perform a background check while offline (no notification)
  CHK_OFFLINE: 17,
  // Note: codes 18 - 21 were removed along with the certificate checking code.
  // General update check failure and threshold reached
  // (check failure notification)
  CHK_GENERAL_ERROR_PROMPT: 22,
  // General update check failure and threshold not reached (no notification)
  CHK_GENERAL_ERROR_SILENT: 23,
  // No compatible update found though there were updates (no notification)
  CHK_NO_COMPAT_UPDATE_FOUND: 24,
  // Update found for a previous version (no notification)
  CHK_UPDATE_PREVIOUS_VERSION: 25,
  // Update found without a type attribute (no notification)
  CHK_UPDATE_INVALID_TYPE: 27,
  // The system is no longer supported (system unsupported notification)
  CHK_UNSUPPORTED: 28,
  // Unable to apply updates (manual install to update notification)
  CHK_UNABLE_TO_APPLY: 29,
  // Unable to check for updates due to no OS version (no notification)
  CHK_NO_OS_VERSION: 30,
  // Unable to check for updates due to no OS ABI (no notification)
  CHK_NO_OS_ABI: 31,
  // Invalid update url (no notification)
  CHK_INVALID_DEFAULT_URL: 32,
  // Update elevation failures or cancelations threshold reached for this
  // version, OSX only (no notification)
  CHK_ELEVATION_DISABLED_FOR_VERSION: 35,
  // User opted out of elevated updates for the available update version, OSX
  // only (no notification)
  CHK_ELEVATION_OPTOUT_FOR_VERSION: 36,
  // Update checks disabled by enterprise policy
  CHK_DISABLED_BY_POLICY: 37,
  // Update check failed due to write error
  CHK_ERR_WRITE_FAILURE: 38,
  // Update check was delayed because another instance of the application is
  // currently running
  CHK_OTHER_INSTANCE: 39,
  // Cannot yet download update because no partial patch is available and an
  // update has already been downloaded.
  CHK_NO_PARTIAL_PATCH: 40,

  /**
   * Submit a telemetry ping for the update check result code or a telemetry
   * ping for a counter metric when no update was found. The no
   * update found ping is separate since it is the typical result, is less
   * interesting than the other result codes, and it is easier to analyze the
   * other codes without including it.
   *
   * @param  aSuffix
   *         The metric name suffix for metric names:
   *         Glean.update.checkCodeExternal
   *         Glean.update.checkCodeNotify
   *         Glean.update.checkCodeSubsequent
   *         Glean.update.checkNoUpdateExternal
   *         Glean.update.checkNoUpdateNotify
   *         Glean.update.checkNoUpdateSubsequent
   * @param  aCode
   *         An integer value as defined by the values that start with CHK_ in
   *         the above section.
   */
  pingCheckCode: function UT_pingCheckCode(aSuffix, aCode) {
    try {
      if (aCode == this.CHK_NO_UPDATE_FOUND) {
        // counter metric
        Glean.update["checkNoUpdate" + aSuffix].add();
      } else {
        // custom distribution metric
        Glean.update["checkCode" + aSuffix].accumulateSingleSample(aCode);
      }
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Submit a telemetry ping for a failed update check's unhandled error code
   * when the pingCheckCode is CHK_GENERAL_ERROR_SILENT. The metric is a labeled
   * counter with label names that are prefixed with 'AUS_CHECK_EX_ERR_'.
   *
   * @param  aSuffix
   *         The metric name suffix for metric names:
   *         Glean.update.checkExtendedErrorExternal
   *         Glean.update.checkExtendedErrorNotify
   *         Glean.update.checkExtendedErrorSubsequent
   * @param  aCode
   *         The extended error value return by a failed update check.
   */
  pingCheckExError: function UT_pingCheckExError(aSuffix, aCode) {
    try {
      // labeled counter metric
      Glean.update["checkExtendedError" + aSuffix][
        "AUS_CHECK_EX_ERR_" + aCode
      ].add();
    } catch (e) {
      console.error(e);
    }
  },

  // The state code and if present the status error code were read on startup.
  STARTUP: "Startup",
  // The state code and status error code if present were read after staging.
  STAGE: "Stage",

  // Patch type Complete
  PATCH_COMPLETE: "Complete",
  // Patch type partial
  PATCH_PARTIAL: "Partial",
  // Patch type unknown
  PATCH_UNKNOWN: "Unknown",

  /**
   * Values for the Glean.update.downloadCodeComplete,
   * Glean.update.downloadCodePartial and
   * Glean.update.downloadCodeUnknown metrics.
   */
  DWNLD_SUCCESS: 0,
  DWNLD_RETRY_OFFLINE: 1,
  DWNLD_RETRY_NET_TIMEOUT: 2,
  DWNLD_RETRY_CONNECTION_REFUSED: 3,
  DWNLD_RETRY_NET_RESET: 4,
  DWNLD_ERR_NO_UPDATE: 5,
  DWNLD_ERR_NO_UPDATE_PATCH: 6,
  DWNLD_ERR_PATCH_SIZE_LARGER: 8,
  DWNLD_ERR_PATCH_SIZE_NOT_EQUAL: 9,
  DWNLD_ERR_BINDING_ABORTED: 10,
  DWNLD_ERR_ABORT: 11,
  DWNLD_ERR_DOCUMENT_NOT_CACHED: 12,
  DWNLD_ERR_VERIFY_NO_REQUEST: 13,
  DWNLD_ERR_VERIFY_PATCH_SIZE_NOT_EQUAL: 14,
  DWNLD_ERR_WRITE_FAILURE: 15,
  // Temporary failure code to see if there are failures without an update phase
  DWNLD_UNKNOWN_PHASE_ERR_WRITE_FAILURE: 40,

  /**
   * Submit a telemetry ping for the update download result code.
   *
   * @param  aIsComplete
   *         If true the patch type is complete, if false the patch type is
   *         partial, and when undefined the patch type is unknown.
   *         This is used to determine the metric ID out of the following:
   *         Glean.update.downloadCodeComplete
   *         Glean.update.downloadCodePartial
   *         Glean.update.downloadCodeUnknown
   * @param  aCode
   *         An integer value as defined by the values that start with DWNLD_ in
   *         the above section.
   */
  pingDownloadCode: function UT_pingDownloadCode(aIsComplete, aCode) {
    let patchType = this.PATCH_UNKNOWN;
    if (aIsComplete === true) {
      patchType = this.PATCH_COMPLETE;
    } else if (aIsComplete === false) {
      patchType = this.PATCH_PARTIAL;
    }
    try {
      // custom_distribution metric
      Glean.update["downloadCode" + patchType].accumulateSingleSample(aCode);
    } catch (e) {
      console.error(e);
    }
  },

  // Previous state codes are defined in pingStateAndStatusCodes() in
  // nsUpdateService.js
  STATE_WRITE_FAILURE: 14,

  /**
   * Submit a telemetry ping for the update status state code.
   *
   * @param  aSuffix
   *         The glean name suffix for glean metric IDs:
   *         Glean.update.stateCodeCompleteStartup
   *         Glean.update.stateCodePartialStartup
   *         Glean.update.stateCodeUnknownStartup
   *         Glean.update.stateCodeCompleteStage
   *         Glean.update.stateCodePartialStage
   *         Glean.update.stateCodeUnknownStage
   * @param  aCode
   *         An integer value as defined by the values that start with STATE_ in
   *         the above section for the update state from the update.status file.
   */
  pingStateCode: function UT_pingStateCode(aSuffix, aCode) {
    try {
      // custom_distribution metric
      Glean.update["stateCode" + aSuffix].accumulateSingleSample(aCode);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Submit a telemetry ping for the update status error code. This does not
   * submit a success value which can be determined from the state code.
   *
   * @param  aSuffix
   *         The glean name suffix for glean metric IDs:
   *         Glean.update.statusErrorCodeCompleteStartup
   *         Glean.update.statusErrorCodePartialStartup
   *         Glean.update.statusErrorCodeUnknownStartup
   *         Glean.update.statusErrorCodeCompleteStage
   *         Glean.update.statusErrorCodePartialStage
   *         Glean.update.statusErrorCodeUnknownStage
   * @param  aCode
   *         An integer value for the error code from the update.status file.
   */
  pingStatusErrorCode: function UT_pingStatusErrorCode(aSuffix, aCode) {
    try {
      // custom_distribution metric
      Glean.update["statusErrorCode" + aSuffix].accumulateSingleSample(aCode);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Records a failed BITS update download using Telemetry.
   * In addition to the BITS Result custom_distribution metric, this also
   * records an update.bits_error event with the patch type, the error type,
   * action and stage, and the nsresult or HRESULT code if there is one.
   *
   * @param aIsComplete
   *        If true the patch type is complete, if false the patch type is
   *        partial. This will determine the metric id out of the following:
   *        Glean.update.bitsResultComplete
   *        Glean.update.bitsResultPartial
   *        It is also recorded as the patch_type of the event.
   * @param aError
   *        The BitsError that occurred. See Bits.sys.mjs for details on BitsError.
   */
  pingBitsError: function UT_pingBitsError(aIsComplete, aError) {
    if (AppConstants.platform != "win") {
      console.error(
        "Warning: Attempted to submit BITS telemetry on a " +
          "non-Windows platform"
      );
      return;
    }
    if (!(aError instanceof BitsError)) {
      console.error("Error sending BITS Error ping: Error is not a BitsError");
      aError = new BitsUnknownError();
    }
    // Coerce the error to integer
    let type = +aError.type;
    if (isNaN(type)) {
      console.error(
        "Error sending BITS Error ping: Either error is not a " +
          "BitsError, or error type is not an integer."
      );
      type = Ci.nsIBits.ERROR_TYPE_UNKNOWN;
    } else if (type == Ci.nsIBits.ERROR_TYPE_SUCCESS) {
      console.error(
        "Error sending BITS Error ping: The error type must not " +
          "be the success type."
      );
      type = Ci.nsIBits.ERROR_TYPE_UNKNOWN;
    }
    this._pingBitsResult(aIsComplete, type);

    let extra = {
      patch_type: aIsComplete ? "complete" : "partial",
      error_type: type,
      error_action: +aError.action,
      error_stage: +aError.stage,
      code_type: BITS_CODE_TYPE_NAMES.get(aError.codeType) ?? "unknown",
    };
    switch (aError.codeType) {
      case Ci.nsIBits.ERROR_CODE_TYPE_NSRESULT:
      case Ci.nsIBits.ERROR_CODE_TYPE_HRESULT:
        extra.code = "0x" + (aError.code >>> 0).toString(16).padStart(8, "0");
        break;
      // String and exception codes are free-form messages that may contain
      // file paths, so only their type is recorded.
    }
    try {
      Glean.update.bitsError.record(extra);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Records a successful BITS update download using Telemetry.
   *
   * @param aIsComplete
   *        If true the patch type is complete, if false the patch type is
   *        partial. This will determine the metric id out of the following:
   *        Glean.update.bitsResultComplete
   *        Glean.update.bitsResultPartial
   */
  pingBitsSuccess: function UT_pingBitsSuccess(aIsComplete) {
    if (AppConstants.platform != "win") {
      console.error(
        "Warning: Attempted to submit BITS telemetry on a " +
          "non-Windows platform"
      );
      return;
    }
    this._pingBitsResult(aIsComplete, Ci.nsIBits.ERROR_TYPE_SUCCESS);
  },

  /**
   * This is the helper function that does all the work for pingBitsError and
   * pingBitsSuccess. It submits a telemetry ping indicating the result of the
   * BITS update download.
   *
   * @param aIsComplete
   *        If true the patch type is complete, if false the patch type is
   *        partial. This will determine the metric id out of the following:
   *        Glean.update.bitsResultComplete
   *        Glean.update.bitsResultPartial
   * @param aResultType
   *        The result code. This will be one of the ERROR_TYPE_* values defined
   *        in the nsIBits interface.
   */
  _pingBitsResult: function UT_pingBitsResult(aIsComplete, aResultType) {
    let patchType;
    if (aIsComplete) {
      patchType = this.PATCH_COMPLETE;
    } else {
      patchType = this.PATCH_PARTIAL;
    }
    try {
      Glean.update["bitsResult" + patchType].accumulateSingleSample(
        aResultType
      );
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Submit the interval in days since the last notification for this background
   * update check or a boolean if the last notification is in the future.
   *
   * @param  aSuffix
   *         The metric id suffix for metric names:
   *         Glean.update.invalidLastupdatetimeExternal
   *         Glean.update.invalidLastupdatetimeNotify
   *         Glean.update.invalidLastupdatetimeSubsequent
   *         Glean.update.lastNotifyIntervalDaysExternal
   *         Glean.update.lastNotifyIntervalDaysNotify
   *         Glean.update.lastNotifyIntervalDaysSubsequent
   */
  pingLastUpdateTime: function UT_pingLastUpdateTime(aSuffix) {
    const PREF_APP_UPDATE_LASTUPDATETIME =
      "app.update.lastUpdateTime.background-update-timer";
    if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_LASTUPDATETIME)) {
      let lastUpdateTimeSeconds = Services.prefs.getIntPref(
        PREF_APP_UPDATE_LASTUPDATETIME
      );
      if (lastUpdateTimeSeconds) {
        let currentTimeSeconds = Math.round(Date.now() / 1000);
        if (lastUpdateTimeSeconds > currentTimeSeconds) {
          try {
            // counter metric
            Glean.update["invalidLastupdatetime" + aSuffix].add();
          } catch (e) {
            console.error(e);
          }
        } else {
          let intervalDays =
            (currentTimeSeconds - lastUpdateTimeSeconds) / (60 * 60 * 24);
          try {
            // timing_distribution metric with day as the unit
            Glean.update[
              "lastNotifyIntervalDays" + aSuffix
            ].accumulateSingleSample(intervalDays);
          } catch (e) {
            console.error(e);
          }
        }
      }
    }
  },

  /**
   * Submit a telemetry ping for a boolean value that indicates if the
   * service is installed and a counter that indicates if the service was
   * at some point installed and is now uninstalled.
   *
   * @param  aSuffix
   *         The metric name suffix for metric names:
   *         Glean.update.serviceInstalledExternal
   *         Glean.update.serviceInstalledNotify
   *         Glean.update.serviceInstalledSubsequent
   *         Glean.update.serviceManuallyUninstalledExternal
   *         Glean.update.serviceManuallyUninstalledNotify
   *         Glean.update.serviceManuallyUninstalledSubsequent
   * @param  aInstalled
   *         Whether the service is installed.
   */
  pingServiceInstallStatus: function UT_PSIS(aSuffix, aInstalled) {
    // Report the error but don't throw since it is more important to
    // successfully update than to throw.
    if (!("@mozilla.org/windows-registry-key;1" in Cc)) {
      console.error(Cr.NS_ERROR_NOT_AVAILABLE);
      return;
    }

    try {
      // labeled_counter metric with "false" and "true" labels
      Glean.update["serviceInstalled" + aSuffix][aInstalled].add();
    } catch (e) {
      console.error(e);
    }

    let attempted = 0;
    try {
      let wrk = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
        Ci.nsIWindowsRegKey
      );
      wrk.open(
        wrk.ROOT_KEY_LOCAL_MACHINE,
        "SOFTWARE\\Mozilla\\MaintenanceService",
        wrk.ACCESS_READ | wrk.WOW64_64
      );
      // Was the service at some point installed, but is now uninstalled?
      attempted = wrk.readIntValue("Attempted");
      wrk.close();
    } catch (e) {
      // Since this will throw if the registry key doesn't exist (e.g. the
      // service has never been installed) don't report an error.
    }

    try {
      if (!aInstalled && attempted) {
        // counter metric
        Glean.update["serviceManuallyUninstalled" + aSuffix].add();
      }
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Submit a telemetry ping for a counter glean metric when the expected value
   * does not equal the boolean value of a pref or if the pref isn't present
   * when the expected value does not equal default value. This lessens the
   * amount of data submitted to telemetry.
   *
   * @param  aMetric
   *         The glean metric to report to.
   * @param  aPref
   *         The preference to check.
   * @param  aDefault
   *         The default value when the preference isn't present.
   * @param  aExpected
   *         The expected value. Matching values are not counted.
   */
  pingBoolPref: function UT_pingBoolPref(aMetric, aPref, aDefault, aExpected) {
    try {
      let val = aDefault;
      if (Services.prefs.getPrefType(aPref) != Ci.nsIPrefBranch.PREF_INVALID) {
        val = Services.prefs.getBoolPref(aPref);
      }
      if (val != aExpected) {
        // counter metric
        aMetric.add();
      }
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Submit a telemetry ping for a glean metric with the integer value of a
   * preference when it is not the expected value or the default value when it
   * is not the expected value. This lessens the amount of data submitted to
   * telemetry.
   *
   * @param  aMetric
   *         The glean metric to report to.
   * @param  aPref
   *         The preference to check.
   * @param  aDefault
   *         The default value when the pref is not set.
   * @param  aExpected (optional)
   *         If specified and the value is the same as the value that will be
   *         added the value won't be added to telemetry.
   */
  pingIntPref: function UT_pingIntPref(aMetric, aPref, aDefault, aExpected) {
    try {
      let val = aDefault;
      if (Services.prefs.getPrefType(aPref) != Ci.nsIPrefBranch.PREF_INVALID) {
        val = Services.prefs.getIntPref(aPref);
      }
      if (aExpected === undefined || val != aExpected) {
        // custom_distribution metric
        aMetric.accumulateSingleSample(val);
      }
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Valid keys for the update.moveresult scalar.
   */
  MOVE_RESULT_SUCCESS: "SUCCESS",
  MOVE_RESULT_UNKNOWN_FAILURE: "UNKNOWN_FAILURE",

  /**
   * Reports the passed result of attempting to move the downloading update
   * into the ready update directory.
   */
  pingMoveResult: function UT_pingMoveResult(aResult) {
    Glean.update.moveResult[aResult].add(1);
  },

  pingSuppressPrompts: function UT_pingSuppressPrompts() {
    try {
      let val = Services.prefs.getBoolPref("app.update.suppressPrompts", false);
      if (val === true) {
        Glean.update.suppressPrompts.set(true);
      }
    } catch (e) {
      console.error(e);
    }
  },

  pingPinPolicy: function UT_pingPinPolicy(updatePin) {
    try {
      Glean.update.versionPin.set(updatePin);
    } catch (e) {
      console.error(e);
    }
  },
};

Object.freeze(AUSTLMY);
