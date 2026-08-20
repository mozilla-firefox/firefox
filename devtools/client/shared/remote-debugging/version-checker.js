/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const MS_PER_DAY = 1000 * 60 * 60 * 24;

const COMPATIBILITY_STATUS = {
  COMPATIBLE: "compatible",
  TOO_OLD: "too-old",
  TOO_OLD_FENNEC: "too-old-fennec",
  TOO_RECENT: "too-recent",
};
exports.COMPATIBILITY_STATUS = COMPATIBILITY_STATUS;

function getDateFromBuildID(buildID) {
  // Build IDs are a timestamp in the yyyyMMddHHmmss format.
  // Extract the year, month and day information.
  const fields = buildID.match(/(\d{4})(\d{2})(\d{2})/);
  // Date expects 0 - 11 for months
  const month = Number.parseInt(fields[2], 10) - 1;
  return new Date(fields[1], month, fields[3]);
}

function getMajorVersion(platformVersion) {
  // Retrieve the major platform version, i.e. if we are on Firefox 64.0a1, it will be 64.
  return Number.parseInt(platformVersion.match(/\d+/)[0], 10);
}

/**
 * Compute the minimum and maximum supported version for remote debugging for the provided
 * version of Firefox. Backward compatibility policy for devtools supports at most 3
 * versions older than the current version.
 *
 * @param {string} localVersion
 *        The version of the local Firefox instance, eg "67.0"
 * @return {object}
 *         - minVersion {String} the minimum supported version, eg "64.0a1"
 *         - maxVersion {String} the first unsupported version, eg "68.0a1"
 */
function computeMinMaxVersion(localVersion) {
  // Retrieve the major platform version, i.e. if we are on Firefox 64.0a1, it will be 64.
  const localMajorVersion = getMajorVersion(localVersion);

  return {
    // Define the minimum officially supported version of Firefox when connecting to a
    // remote runtime. (Use ".0a1" to support the very first nightly version)
    // DevTools backward compatibility policy allows to debug Firefox Release
    // from Nightly, which are usually 2 versions apart, but around release time
    // Nightly is bumped before release, so we accept 3 versions of gap.
    minVersion: localMajorVersion - 3 + ".0a1",
    // The maximum version is the first excluded from the support range. That's why we
    // increase the current version by 1 and use ".0a1" to point to the first Nightly.
    // We do not support forward compatibility at all.
    maxVersion: localMajorVersion + 1 + ".0a1",
  };
}

/**
 * Tells if the remote device is using a supported version of Firefox.
 *
 * @param {DevToolsClient} devToolsClient
 *        DevToolsClient instance connected to the target remote Firefox.
 * @return Object with the following attributes:
 *   * String status, one of COMPATIBILITY_STATUS
 *            COMPATIBLE if the runtime is compatible,
 *            TOO_RECENT if the runtime uses a too recent version,
 *            TOO_OLD if the runtime uses a too old version.
 *   * String minVersion
 *            The minimum supported version.
 *   * String runtimeVersion
 *            The remote runtime version.
 *   * String localID
 *            Build ID of local runtime. A date with like this: YYYYMMDD.
 *   * String deviceID
 *            Build ID of remote runtime. A date with like this: YYYYMMDD.
 */
async function checkVersionCompatibility(devToolsClient) {
  const localDescription = {
    appbuildid: Services.appinfo.appBuildID,
    platformversion: AppConstants.MOZ_APP_VERSION,
  };

  try {
    const deviceFront = await devToolsClient.mainRoot.getFront("device");
    const description = await deviceFront.getDescription();
    return _compareVersionCompatibility(localDescription, description);
  } catch (e) {
    // If we failed to retrieve the device description, assume we are trying to connect to
    // a really old version of Firefox.
    const localVersion = localDescription.platformversion;
    const { minVersion } = computeMinMaxVersion(localVersion);
    return {
      minVersion,
      runtimeVersion: "<55",
      status: COMPATIBILITY_STATUS.TOO_OLD,
    };
  }
}
exports.checkVersionCompatibility = checkVersionCompatibility;

function _compareVersionCompatibility(localDescription, deviceDescription) {
  const runtimeID = deviceDescription.appbuildid.substr(0, 8);
  const localID = localDescription.appbuildid.substr(0, 8);

  const runtimeDate = getDateFromBuildID(runtimeID);
  const localDate = getDateFromBuildID(localID);

  const runtimeVersion = deviceDescription.platformversion;
  const localVersion = localDescription.platformversion;

  const { minVersion, maxVersion } = computeMinMaxVersion(localVersion);
  const isTooOld = Services.vc.compare(runtimeVersion, minVersion) < 0;
  const isTooRecent = Services.vc.compare(runtimeVersion, maxVersion) >= 0;

  const runtimeMajorVersion = getMajorVersion(runtimeVersion);
  const localMajorVersion = getMajorVersion(localVersion);
  const isSameMajorVersion = runtimeMajorVersion === localMajorVersion;

  let status;
  if (isTooOld) {
    if (runtimeMajorVersion === 68 && deviceDescription.os === "Android") {
      status = COMPATIBILITY_STATUS.TOO_OLD_FENNEC;
    } else {
      status = COMPATIBILITY_STATUS.TOO_OLD;
    }
  } else if (isTooRecent) {
    status = COMPATIBILITY_STATUS.TOO_RECENT;
  } else if (isSameMajorVersion && runtimeDate - localDate > 7 * MS_PER_DAY) {
    // If both local and remote runtimes have the same major version, compare build dates.
    // This check is useful for Gecko developers as we might introduce breaking changes
    // within a Nightly cycle.
    // Still allow devices to be newer by up to a week. This accommodates those with local
    // device builds, since their devices will almost always be newer than the client.
    status = COMPATIBILITY_STATUS.TOO_RECENT;
  } else {
    status = COMPATIBILITY_STATUS.COMPATIBLE;
  }

  return {
    localID,
    localVersion,
    minVersion,
    runtimeID,
    runtimeVersion,
    status,
  };
}
// Exported for tests.
exports._compareVersionCompatibility = _compareVersionCompatibility;
