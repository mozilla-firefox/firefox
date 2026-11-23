/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  SanityCheckResult,
  runSanityTest,
} from "resource://gre/modules/SanityTestChecks.sys.mjs";

const TEST_DISABLED_PREF = "media.sanity-test.disabled";
const DRIVER_PREF = "sanity-test.driver-version";
const DEVICE_PREF = "sanity-test.device-id";
const DRIVER2_PREF = "sanity-test.driver-version2";
const DEVICE2_PREF = "sanity-test.device-id2";
const VERSION_PREF = "sanity-test.version";
const DISABLE_VIDEO_DECODE_PREF = "media.hardware-video-decoding.failed";
const DISABLE_VIDEO_ENCODE_PREF = "media.hardware-video-encoding.failed";
const RUNNING_PREF = "sanity-test.running";

function annotateCrashReport() {
  try {
    Services.appinfo.annotateCrashReport("TestKey", "1");
  } catch (e) {}
}

function removeCrashReportAnnotation() {
  try {
    Services.appinfo.removeCrashReportAnnotation("TestKey");
  } catch (e) {}
}

function onSanityTestComplete(result) {
  ChromeUtils.addProfilerMarker(
    "GfxSanityTest",
    { category: "Graphics" },
    `result: ${Object.keys(SanityCheckResult).find(key => SanityCheckResult[key] == result)}`
  );
  switch (result) {
    case SanityCheckResult.FailedVideoDecode:
    case SanityCheckResult.Crashed:
      Services.prefs.setBoolPref(DISABLE_VIDEO_DECODE_PREF, true);
      break;
    case SanityCheckResult.FailedVideoEncode:
      Services.prefs.setBoolPref(DISABLE_VIDEO_ENCODE_PREF, true);
      break;
  }

  // If there is no hardware encoding support, it is the same as passed.
  Glean.gfx.sanityTest.accumulateSingleSample(
    result == SanityCheckResult.PassedNoHardwareEncoder
      ? SanityCheckResult.Passed
      : result
  );
  Services.prefs.setBoolPref(RUNNING_PREF, false);
  Services.prefs.savePrefFile(null);
}

function onSanityTestError(e) {
  console.error("Graphics sanity test failed to run: ", e);
  onSanityTestComplete(SanityCheckResult.Timeout);
}

export function SanityTest() {}
SanityTest.prototype = {
  classID: Components.ID("{f3a8ca4d-4c83-456b-aee2-6a2cbf11e9bd}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  shouldRunTest() {
    // Only test gfx features if firefox has updated, or if the user has a new
    // gpu or drivers.
    var buildId = Services.appinfo.platformBuildID;
    var gfxinfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);

    if (Services.prefs.getBoolPref(RUNNING_PREF, false)) {
      onSanityTestComplete(SanityCheckResult.Crashed);
      return false;
    }

    function checkPref(pref, value) {
      let prefValue;
      let prefType = Services.prefs.getPrefType(pref);

      switch (prefType) {
        case Ci.nsIPrefBranch.PREF_INVALID:
          return false;

        case Ci.nsIPrefBranch.PREF_STRING:
          prefValue = Services.prefs.getStringPref(pref);
          break;

        case Ci.nsIPrefBranch.PREF_BOOL:
          prefValue = Services.prefs.getBoolPref(pref);
          break;

        case Ci.nsIPrefBranch.PREF_INT:
          prefValue = Services.prefs.getIntPref(pref);
          break;

        default:
          throw new Error("Unexpected preference type.");
      }

      return prefValue == value;
    }

    // The secondary adapter matters as much as the primary one here. MFTEnumEx
    // does not filter by adapter, so a hardware encoder belonging to the
    // secondary device can be the one we end up using, and blocklist entries
    // can match against it. Both are empty strings on a single GPU machine.
    if (
      checkPref(DRIVER_PREF, gfxinfo.adapterDriverVersion) &&
      checkPref(DEVICE_PREF, gfxinfo.adapterDeviceID) &&
      checkPref(DRIVER2_PREF, gfxinfo.adapterDriverVersion2) &&
      checkPref(DEVICE2_PREF, gfxinfo.adapterDeviceID2) &&
      checkPref(VERSION_PREF, buildId)
    ) {
      return false;
    }

    // Enable hardware decoding and encoding so we can test again
    // and record the driver version to detect if the driver changes.
    Services.prefs.setBoolPref(DISABLE_VIDEO_DECODE_PREF, false);
    Services.prefs.setBoolPref(DISABLE_VIDEO_ENCODE_PREF, false);
    Services.prefs.setStringPref(DRIVER_PREF, gfxinfo.adapterDriverVersion);
    Services.prefs.setStringPref(DEVICE_PREF, gfxinfo.adapterDeviceID);
    Services.prefs.setStringPref(DRIVER2_PREF, gfxinfo.adapterDriverVersion2);
    Services.prefs.setStringPref(DEVICE2_PREF, gfxinfo.adapterDeviceID2);
    Services.prefs.setStringPref(VERSION_PREF, buildId);

    // Update the prefs so that this test doesn't run again until the next update.
    Services.prefs.setBoolPref(RUNNING_PREF, true);
    Services.prefs.savePrefFile(null);
    return true;
  },

  observe(subject, topic) {
    if (topic != "profile-after-change") {
      return;
    }
    if (Services.prefs.getBoolPref(TEST_DISABLED_PREF, false)) {
      return;
    }

    if (!this.shouldRunTest()) {
      return;
    }

    annotateCrashReport();

    runSanityTest()
      .then(onSanityTestComplete, onSanityTestError)
      .finally(removeCrashReportAnnotation);
  },
};
