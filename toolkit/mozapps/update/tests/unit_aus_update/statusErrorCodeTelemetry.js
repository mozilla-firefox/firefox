/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Tests that the updater's error code from update.status is reported via the
 * update.status_error_code_*_startup metrics when post update processing runs.
 *
 * See bug 2066420: the status string was truncated at the colon before it
 * reached pingStateAndStatusCodes(), so no updater error code was ever
 * recorded on the startup (non-staged) path.
 */

add_setup(async function setup() {
  setupTestCommon();

  // FOG needs a profile directory to put its data in.
  do_get_profile();
  Services.fog.initializeFOG();
});

/**
 * Writes an active update with a single selected patch of the given type and a
 * status file with the given contents, then runs post update processing.
 *
 * @param  patchType
 *         "complete" or "partial".
 * @param  status
 *         The raw contents to write to update.status.
 */
async function runPostUpdateProcessing(patchType, status) {
  Services.fog.testResetFOG();

  const patches = getLocalPatchString({
    type: patchType,
    state: STATE_PENDING,
  });
  writeUpdatesToXMLFile(
    getLocalUpdatesXMLString(
      getLocalUpdateString({ appVersion: "2" }, patches)
    ),
    true
  );
  writeFile(getUpdateDirFile(FILE_UPDATE_MAR, DIR_PATCH), "test mar contents");
  writeStatusFile(status);

  await reloadUpdateManagerData();
  await testPostUpdateProcessing();
}

/**
 * @param  metric
 *         A custom_distribution metric from Glean.update.
 * @return An object with the number of samples accumulated into the metric and
 *         their sum. Using the sum rather than the bucket keys keeps this
 *         independent of how Glean lays out the buckets.
 */
function summarize(metric) {
  const value = metric.testGetValue();
  if (!value) {
    return { count: 0, sum: 0 };
  }
  let count = 0;
  for (const bucketCount of Object.values(value.values)) {
    count += bucketCount;
  }
  return { count, sum: Number(value.sum) };
}

add_task(async function testPartialStatusErrorCodeRecorded() {
  await runPostUpdateProcessing("partial", STATE_FAILED_READ_ERROR);

  Assert.deepEqual(
    summarize(Glean.update.statusErrorCodePartialStartup),
    { count: 1, sum: READ_ERROR },
    "the updater's error code should be recorded for a partial patch"
  );
  Assert.deepEqual(
    summarize(Glean.update.statusErrorCodeCompleteStartup),
    { count: 0, sum: 0 },
    "the complete patch metric should not be touched"
  );
});

add_task(async function testCompleteStatusErrorCodeRecorded() {
  await runPostUpdateProcessing("complete", STATE_FAILED_READ_ERROR);

  Assert.deepEqual(
    summarize(Glean.update.statusErrorCodeCompleteStartup),
    { count: 1, sum: READ_ERROR },
    "the updater's error code should be recorded for a complete patch"
  );
});

add_task(async function testStatusErrorCodeWithoutDelimiter() {
  // A "failed" status with no error code at all should still be reported, as
  // INVALID_UPDATER_STATUS_CODE, rather than silently recording nothing.
  await runPostUpdateProcessing("partial", STATE_FAILED);

  Assert.deepEqual(
    summarize(Glean.update.statusErrorCodePartialStartup),
    { count: 1, sum: INVALID_UPDATER_STATUS_CODE },
    "a failed status with no error code should record the invalid status code"
  );
});

add_task(async function testNonFailedStatusRecordsNoErrorCode() {
  await runPostUpdateProcessing("partial", STATE_SUCCEEDED);

  Assert.deepEqual(
    summarize(Glean.update.statusErrorCodePartialStartup),
    { count: 0, sum: 0 },
    "a non-failed status should not record an error code"
  );

  await doTestFinish();
});
