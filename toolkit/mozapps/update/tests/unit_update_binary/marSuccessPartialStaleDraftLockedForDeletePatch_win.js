/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/* Partial MAR file patch apply success test where the stale draft file of a
 * patched file cannot be removed and must be moved to tobedeleted instead. */

const STALE_DRAFT_NAME = "searchpluginspng0.png.moz-draft";
const STALE_DRAFT_CONTENTS = "ShouldBeMovedToBeDeletedByTheUpdater\n";

/**
 * A file that a previous updater instance would have left behind if it had
 * crashed during the Draft phase, and that this updater instance won't be able
 * to remove. It should be gone from the searchplugins directory once the update
 * has been applied, so it is declared without contents to compare against.
 */
const gStaleDraftFiles = [
  {
    description: "Left behind by a crashed updater, locked against deletion",
    fileName: STALE_DRAFT_NAME,
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: STALE_DRAFT_CONTENTS,
    compareContents: null,
    originalFile: null,
    compareFile: null,
    originalPerms: null,
    comparePerms: null,
  },
];

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  gTestFiles = gTestFilesPartialSuccess.concat(gStaleDraftFiles);
  gTestDirs = gTestDirsPartialSuccess;
  await setupUpdaterTest(FILE_PARTIAL_MAR, false);

  Services.env.set("MOZ_TEST_ENSURE_REMOVE_FAIL", STALE_DRAFT_NAME);

  try {
    runUpdate(STATE_SUCCEEDED, false, 0, true);
  } finally {
    Services.env.set("MOZ_TEST_ENSURE_REMOVE_FAIL", "");
  }

  await checkPostUpdateAppLog();
  checkAppBundleModTime();
  await testPostUpdateProcessing();
  checkPostUpdateRunningFile(true);
  checkFilesAfterUpdateSuccess(getApplyDirFile, false, true);

  // The updater has to fail to remove the draft, otherwise it wouldn't have to
  // move it to tobedeleted. Moving it out of the way is what lets ApplyPatchTo
  // open the draft it writes exclusively.
  checkUpdateLogContains(
    "draft_discard: unable to remove: " +
      DIR_RESOURCES +
      "searchplugins/" +
      STALE_DRAFT_NAME
  );

  let relocatedFiles = checkToBeDeletedFileCount(1);
  // Compare before asserting, to keep the contents of the files out of the log.
  let isStaleDraft = readFileBytes(relocatedFiles[0]) == STALE_DRAFT_CONTENTS;
  Assert.ok(isStaleDraft, "the relocated file should be the stale draft file");

  checkUpdateLogContents(LOG_PARTIAL_SUCCESS);
  await waitForUpdateXMLFiles();
  await checkUpdateManager(STATE_NONE, false, STATE_SUCCEEDED, 0, 1);
  checkCallbackLog();
}
