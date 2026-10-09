/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Partial MAR rollback test where a new-version file cannot be removed and
 * must be moved to tobedeleted during rollback. */

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  gTestFiles = gTestFilesPartialSuccess;
  gTestDirs = gTestDirsPartialSuccess;
  setTestFilesAndDirsForFailure();
  await setupUpdaterTest(FILE_PARTIAL_MAR, false);

  // 1/10/10text0 is only ever removed by this update, so locking it doesn't
  // keep the updater from producing the new contents of the files that the
  // update does replace. The update fails while removing it, which happens
  // after 0/0exe0.exe has been replaced by its new version: rolling back the
  // update has to restore the old version of that file.
  await runHelperLockFile(getTestFileByName("10text0"));
  // Make the removal of the new version of 0/0exe0.exe fail during rollback, so
  // that the updater has to move it to tobedeleted to restore the old version.
  Services.env.set("MOZ_TEST_ENSURE_REMOVE_FAIL", "0exe0.exe");

  try {
    runUpdate(STATE_FAILED_WRITE_ERROR, false, 1, true);
  } finally {
    Services.env.set("MOZ_TEST_ENSURE_REMOVE_FAIL", "");
  }

  await waitForHelperExit();
  checkAppBundleModTime();
  await testPostUpdateProcessing();
  checkPostUpdateRunningFile(false);
  checkFilesAfterUpdateFailure(getApplyDirFile, false, true);

  // The update has to fail while removing the locked file, otherwise it would
  // have nothing to roll back.
  checkUpdateLogContains("EXECUTE REMOVEFILE 1/10/10text0");
  checkUpdateLogContains(ERR_BACKUP_CREATE_7);

  let relocatedFiles = checkToBeDeletedFileCount(1);
  // Compare before asserting, to keep the contents of the files out of the log.
  let isNewVersionExe =
    readFileBytes(relocatedFiles[0]) ==
    readFileBytes(getTestDirFile(FILE_PARTIAL_EXE));
  Assert.ok(
    isNewVersionExe,
    "the relocated file should be the new version of 0/0exe0.exe"
  );

  await waitForUpdateXMLFiles(true, false);
  await checkUpdateManager(STATE_PENDING, true, STATE_PENDING, WRITE_ERROR, 0);
  checkCallbackLog();
}
