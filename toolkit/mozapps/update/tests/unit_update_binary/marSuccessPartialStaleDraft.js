/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/* Partial MAR file patch apply success test with stale draft files */

/**
 * Files that a previous updater instance would have left behind if it had
 * crashed during the Draft phase. They should be gone once the update has been
 * applied, so they are declared without contents to compare against.
 *
 * Both of these are drafts for a file that the MAR does touch. The updater
 * doesn't sweep the installation directory for drafts: it discards the draft of
 * a file right before producing its new contents, so a draft for a path that no
 * update ever touches again survives, which is why seeding one here would make
 * the check for leftover temporary files fail. Such a draft is bounded to the
 * installation it was left in, since staging doesn't copy drafts into the
 * directory that replaces it.
 */
const gStaleDraftFiles = [
  {
    description: "Left behind by a crashed updater (add)",
    fileName: "searchpluginstext0.moz-draft",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: "ShouldBeRemovedByTheUpdater\n",
    compareContents: null,
    originalFile: null,
    compareFile: null,
    // Deliberately not the mode that the MAR gives to searchpluginstext0
    // (0o644): writing the new contents of an added file goes through creat,
    // which only applies its mode argument when it creates the file, so if the
    // updater failed to discard this draft its mode would end up on the
    // installed file and checkFilesAfterUpdateSuccess would notice.
    originalPerms: 0o777,
    comparePerms: null,
  },
  {
    description: "Left behind by a crashed updater (patch)",
    fileName: "searchpluginspng0.png.moz-draft",
    relPathDir: DIR_RESOURCES + "searchplugins/",
    originalContents: "ShouldBeRemovedByTheUpdater\n",
    compareContents: null,
    originalFile: null,
    compareFile: null,
    // No point in picking a mode here: writing the new contents of a patched
    // file goes through ensure_open, which chmods unconditionally.
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

  runUpdate(STATE_SUCCEEDED, false, 0, true);
  await checkPostUpdateAppLog();
  checkAppBundleModTime();
  await testPostUpdateProcessing();
  checkPostUpdateRunningFile(true);
  checkFilesAfterUpdateSuccess(getApplyDirFile);

  // Writing the new contents of a file just truncates its stale draft, so the
  // update would succeed whether the updater discarded that draft or not. Check
  // that it did discard it, rather than rely on a side effect of not doing so.
  for (const staleDraftFile of gStaleDraftFiles) {
    checkUpdateLogContains(
      "draft_discard: discarding draft file: " +
        staleDraftFile.relPathDir +
        staleDraftFile.fileName
    );
  }

  checkUpdateLogContents(LOG_PARTIAL_SUCCESS);
  await waitForUpdateXMLFiles();
  await checkUpdateManager(STATE_NONE, false, STATE_SUCCEEDED, 0, 1);
  checkCallbackLog();
}
