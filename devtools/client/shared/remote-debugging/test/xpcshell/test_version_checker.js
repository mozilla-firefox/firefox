/* global equal */

"use strict";

const {
  _compareVersionCompatibility,
  checkVersionCompatibility,
  COMPATIBILITY_STATUS,
} = require("resource://devtools/client/shared/remote-debugging/version-checker.js");

const TEST_DATA = [
  {
    description: "same build date and same version number",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version in range (-1)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "59.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version in range (-2)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "58.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version in range (-2 Nightly)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "58.0a1",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description:
      "same build date and older version in range (-3 Nightly -> Release during release window)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "57.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version out of range (-4)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "56.0",
    expected: COMPATIBILITY_STATUS.TOO_OLD,
  },
  {
    description: "same build date and newer version out of range (+1)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "61.0",
    expected: COMPATIBILITY_STATUS.TOO_RECENT,
  },
  {
    description: "same major version and build date in range (-10 days)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190121000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same major version and build date in range (+2 days)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same major version and build date out of range (+8 days)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190208000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.TOO_RECENT,
  },
  {
    description:
      "fennec 68 compatibility error not raised for 68 -> 68 Android",
    localBuildId: "20190131000000",
    localVersion: "68.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "68.0",
    runtimeOs: "Android",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description:
      "fennec 68 compatibility error not raised for 70 -> 68 Android",
    localBuildId: "20190131000000",
    localVersion: "70.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "68.0",
    runtimeOs: "Android",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description:
      "fennec 68 compatibility error not raised for 71 -> 68 Android",
    localBuildId: "20190131000000",
    localVersion: "71.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "68.0",
    runtimeOs: "Android",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "fennec 68 compatibility error raised for 72 -> 68 Android",
    localBuildId: "20190131000000",
    localVersion: "72.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "68.0",
    runtimeOs: "Android",
    expected: COMPATIBILITY_STATUS.TOO_OLD_FENNEC,
  },
  {
    description:
      "fennec 68 compatibility error not raised for 72 -> 68 non-Android",
    localBuildId: "20190131000000",
    localVersion: "72.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "68.0",
    runtimeOs: "NotAndroid",
    expected: COMPATIBILITY_STATUS.TOO_OLD,
  },
];

add_task(async function testVersionChecker() {
  for (const testData of TEST_DATA) {
    const localDescription = {
      appbuildid: testData.localBuildId,
      platformversion: testData.localVersion,
    };

    const runtimeDescription = {
      appbuildid: testData.runtimeBuildId,
      platformversion: testData.runtimeVersion,
      os: testData.runtimeOs,
    };

    const report = _compareVersionCompatibility(
      localDescription,
      runtimeDescription
    );
    equal(
      report.status,
      testData.expected,
      "Expected status for test: " + testData.description
    );
  }
});

add_task(async function testVersionCheckWithVeryOldClient() {
  // Use an empty object as devtools client, calling any method on it will fail.
  const emptyClient = {};
  const report = await checkVersionCompatibility(emptyClient);
  equal(
    report.status,
    COMPATIBILITY_STATUS.TOO_OLD,
    "Report status too old if devtools client is not implementing expected interface"
  );
});
