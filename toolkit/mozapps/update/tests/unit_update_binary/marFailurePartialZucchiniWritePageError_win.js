/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Zucchini Partial MAR File Patch Apply In-Page Error Test */

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  const pageErrorEnv = "MOZ_TEST_ZUCCHINI_WRITE_PAGE_ERROR";
  const hadPageErrorEnv = Services.env.exists(pageErrorEnv);
  const originalPageErrorEnv = hadPageErrorEnv
    ? Services.env.get(pageErrorEnv)
    : "";
  Services.env.set(pageErrorEnv, "1");
  registerCleanupFunction(() => {
    Services.env.set(pageErrorEnv, hadPageErrorEnv ? originalPageErrorEnv : "");
  });
  gTestFiles = gTestFilesPartialSuccess;
  gTestDirs = gTestDirsPartialSuccess;
  setTestFilesAndDirsForFailure();
  await setupUpdaterTest(FILE_PARTIAL_ZUCCHINI_MAR, false);
  runUpdate(
    STATE_FAILED_UNEXPECTED_BSPATCH_ERROR,
    false,
    USE_EXECV ? 0 : 1,
    true
  );
  checkAppBundleModTime();
  await testPostUpdateProcessing();
  checkPostUpdateRunningFile(false);
  checkUpdateLogContains(
    "EXCEPTION_IN_PAGE_ERROR while writing to mapped files"
  );
  // kStatusDiskFull
  checkUpdateLogContains(
    "FromZucchiniStatus: encountered zucchini error 8, converting to updater " +
      "error " +
      UNEXPECTED_BSPATCH_ERROR
  );
  // Recovering from the in-page error must release the mapped draft, otherwise
  // the updater can neither remove it nor move it out of the way.
  checkUpdateLogContains(
    "draft_discard: discarding draft file: searchplugins/searchpluginspng1.png" +
      ".moz-draft"
  );
  Assert.ok(
    !getLogFileContents().includes("draft_discard: unable to remove"),
    "the updater should be able to remove the draft"
  );
  checkFilesAfterUpdateFailure(getApplyDirFile);
  await waitForUpdateXMLFiles();
  await checkUpdateManager(
    STATE_NONE,
    false,
    STATE_FAILED,
    UNEXPECTED_BSPATCH_ERROR,
    1
  );
  checkCallbackLog();
}
