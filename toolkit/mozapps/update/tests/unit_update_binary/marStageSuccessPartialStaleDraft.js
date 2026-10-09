/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/* Partial MAR file staged patch apply success test with a stale draft file */

const STALE_DRAFT_NAME = "searchpluginstext0.moz-draft";
const STALE_DRAFT_REL_PATH_DIR = DIR_RESOURCES + "searchplugins/";

/**
 * A file that a previous non-staged updater instance would have left behind if
 * it had crashed during the Draft phase. A staged update doesn't discard drafts,
 * since it doesn't write any, but it must not copy this one into the directory
 * that replaces the installation either. It is therefore declared without
 * contents to compare against: it must be absent both from the staging
 * directory and from the installation directory once the update is applied.
 */
const gStaleDraftFiles = [
  {
    description: "Left behind by a crashed non-staged updater",
    fileName: STALE_DRAFT_NAME,
    relPathDir: STALE_DRAFT_REL_PATH_DIR,
    originalContents: "ShouldNotBeStagedByTheUpdater\n",
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
  const STATE_AFTER_STAGE = gIsServiceTest ? STATE_APPLIED_SVC : STATE_APPLIED;
  gTestFiles = gTestFilesPartialSuccess.concat(gStaleDraftFiles);
  const channelPrefs = getTestFileByName(FILE_CHANNEL_PREFS);
  channelPrefs.originalContents = null;
  channelPrefs.compareContents = "FromPartial\n";
  channelPrefs.comparePerms = 0o644;
  gTestDirs = gTestDirsPartialSuccess;
  preventDistributionFiles();
  await setupUpdaterTest(FILE_PARTIAL_MAR, true);
  await stageUpdate(STATE_AFTER_STAGE, true);
  checkPostUpdateRunningFile(false);

  // Staging doesn't touch the installation directory, so the stale draft is
  // still there. Get rid of it before the checks below, which require that no
  // temporary file is left behind in the installation directory: the update
  // itself gets rid of it by replacing that whole directory, which is checked
  // after the switch below.
  const staleDraft = getApplyDirFile(
    STALE_DRAFT_REL_PATH_DIR + STALE_DRAFT_NAME
  );
  Assert.ok(
    staleDraft.exists(),
    MSG_SHOULD_EXIST + getMsgPath(staleDraft.path)
  );
  staleDraft.remove(false);

  checkFilesAfterUpdateSuccess(getStageDirFile, true);
  checkUpdateLogContents(LOG_PARTIAL_SUCCESS, true, false, true);
  // Switch the application to the staged application that was updated.
  runUpdate(STATE_SUCCEEDED, true, 0, true);
  await checkPostUpdateAppLog();
  checkAppBundleModTime();
  await testPostUpdateProcessing();
  checkPostUpdateRunningFile(true);
  checkFilesAfterUpdateSuccess(getApplyDirFile, false, true);
  checkUpdateLogContents(LOG_REPLACE_SUCCESS, false, true, true);
  await waitForUpdateXMLFiles();
  await checkUpdateManager(STATE_NONE, false, STATE_SUCCEEDED, 0, 1);
  checkCallbackLog();
}
