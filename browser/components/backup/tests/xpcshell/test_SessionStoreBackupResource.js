/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SessionStoreBackupResource } = ChromeUtils.importESModule(
  "resource:///modules/backup/SessionStoreBackupResource.sys.mjs"
);
const { SessionStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs"
);
const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

const TOTAL_COOKIES = 10;

add_setup(async () => {
  // Start the cookieservice, to force creation of a database.
  // Get the sessionCookies to join the initialization in cookie thread
  Services.cookies.sessionCookies;

  Services.prefs.setBoolPref(
    "network.cookieJarSettings.unblocked_for_testing",
    true
  );

  let uri = NetUtil.newURI("https://foo.com/");
  let channel = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  });
  for (let i = 0; i < TOTAL_COOKIES; ++i) {
    uri = NetUtil.newURI("https://" + i + ".com/");
    Services.cookies.setCookieStringFromHttp(uri, "oh=hai", channel);
  }

  Assert.equal(Services.cookies.sessionCookies.length, TOTAL_COOKIES);

  let state = SessionStore.getCurrentState(true);
  Assert.equal(
    state.cookies.length,
    TOTAL_COOKIES,
    "The cookies are part of the session"
  );
});

/**
 * Tests that we can measure the Session Store JSON and backups directory.
 */
add_task(async function test_measure() {
  const EXPECTED_KILOBYTES_FOR_BACKUPS_DIR = 1000;
  Services.fog.testResetFOG();

  // Create the sessionstore-backups directory.
  let tempDir = PathUtils.tempDir;
  let sessionStoreBackupsPath = PathUtils.join(
    tempDir,
    "sessionstore-backups",
    "restore.jsonlz4"
  );
  await createKilobyteSizedFile(
    sessionStoreBackupsPath,
    EXPECTED_KILOBYTES_FOR_BACKUPS_DIR
  );

  let sessionStoreBackupResource = new SessionStoreBackupResource();
  await sessionStoreBackupResource.measure(tempDir);

  let sessionStoreBackupsDirectoryMeasurement =
    Glean.browserBackup.sessionStoreBackupsDirectorySize.testGetValue();
  let sessionStoreMeasurement =
    Glean.browserBackup.sessionStoreSize.testGetValue();

  Assert.equal(
    sessionStoreBackupsDirectoryMeasurement,
    EXPECTED_KILOBYTES_FOR_BACKUPS_DIR,
    "Should have collected the correct glean measurement for the sessionstore-backups directory"
  );

  // Session store measurement is from `getCurrentState`, so exact size is unknown.
  Assert.greater(
    sessionStoreMeasurement,
    0,
    "Should have collected a measurement for the session store"
  );

  await IOUtils.remove(sessionStoreBackupsPath);
});

/**
 * Test that the backup method correctly copies items from the profile directory
 * into the staging directory when the backup is encrypted.
 */
add_task(async function test_backup_encrypted() {
  await testBackupHelper(true /* isEncrypted */);
});

/**
 * Test that the backup method correctly copies items from the profile directory
 * into the staging directory when the backup is not encrypted.
 */
add_task(async function test_backup_not_encrypted() {
  await testBackupHelper(false /* isEncrypted */);
});

/**
 * A helper method that does the work of setting up a SessionStoreBackupResource
 * and all of the testing infrastructure needed to test its backup() method.
 *
 * @param {boolean} isEncrypted
 *   True if the test is for an encrypted backup.
 */
async function testBackupHelper(isEncrypted) {
  let sandbox = sinon.createSandbox();

  let sessionStoreBackupResource = new SessionStoreBackupResource();
  let sourcePath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "SessionStoreBackupResource-source-test"
  );
  let stagingPath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "SessionStoreBackupResource-staging-test"
  );

  const simpleCopyFiles = [
    { path: ["sessionstore-backups", "test-sessionstore-backup.jsonlz4"] },
    { path: ["sessionstore-backups", "test-sessionstore-recovery.baklz4"] },
  ];
  await createTestFiles(sourcePath, simpleCopyFiles);

  let sessionStoreState = SessionStore.getCurrentState(true);
  let manifestEntry = await sessionStoreBackupResource.backup(
    stagingPath,
    sourcePath,
    isEncrypted
  );
  Assert.equal(
    manifestEntry,
    null,
    "SessionStoreBackupResource.backup should return null as its ManifestEntry"
  );

  /**
   * We don't expect the actual file sessionstore.jsonlz4 to exist in the profile directory before calling the backup method.
   * Instead, verify that it is created by the backup method and exists in the staging folder right after.
   */
  await assertFilesExist(stagingPath, [
    ...simpleCopyFiles,
    { path: "sessionstore.jsonlz4" },
  ]);

  /**
   * Do a deep comparison between the recorded session state before backup and the file made in the staging folder
   * to verify that information about session state was correctly written for backup.
   */
  let sessionStoreStateStaged = await IOUtils.readJSON(
    PathUtils.join(stagingPath, "sessionstore.jsonlz4"),
    { decompress: true }
  );

  /**
   * These timestamps might be slightly different from one another, so we'll exclude
   * them from the comparison.
   */
  delete sessionStoreStateStaged.session.lastUpdate;
  delete sessionStoreState.session.lastUpdate;

  // Expect that the copy does not include cookies.
  Assert.equal(
    sessionStoreStateStaged.cookies.length,
    0,
    "expected no cookies in copied session state"
  );
  // Delete the cookies from the original, to allow comparison with deepEqual
  sessionStoreState.cookies = [];

  Assert.deepEqual(
    sessionStoreStateStaged,
    sessionStoreState,
    "sessionstore.jsonlz4 in the staging folder matches the recorded session state"
  );

  await maybeRemovePath(stagingPath);
  await maybeRemovePath(sourcePath);

  sandbox.restore();
}

/**
 * Test that the recover method correctly copies items from the recovery
 * directory into the destination profile directory.
 */
add_task(async function test_recover() {
  let sessionStoreBackupResource = new SessionStoreBackupResource();
  let recoveryPath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "SessionStoreBackupResource-recovery-test"
  );
  let destProfilePath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "SessionStoreBackupResource-test-profile"
  );

  const simpleCopyFiles = [
    { path: ["sessionstore-backups", "test-sessionstore-backup.jsonlz4"] },
    { path: ["sessionstore-backups", "test-sessionstore-recovery.baklz4"] },
  ];
  await createTestFiles(recoveryPath, simpleCopyFiles);

  // We backup a copy of sessionstore.jsonlz4, so ensure it exists in the recovery path
  let sessionStoreState = SessionStore.getCurrentState(true);
  let sessionStoreBackupPath = PathUtils.join(
    recoveryPath,
    "sessionstore.jsonlz4"
  );
  await IOUtils.writeJSON(sessionStoreBackupPath, sessionStoreState, {
    compress: true,
  });

  // The backup method is expected to have returned a null ManifestEntry
  let postRecoveryEntry = await sessionStoreBackupResource.recover(
    null /* manifestEntry */,
    recoveryPath,
    destProfilePath
  );
  Assert.equal(
    postRecoveryEntry,
    null,
    "SessionStoreBackupResource.recover should return null as its post recovery entry"
  );

  await assertFilesExist(destProfilePath, [
    ...simpleCopyFiles,
    { path: "sessionstore.jsonlz4" },
  ]);

  let sessionStateCopied = await IOUtils.readJSON(
    PathUtils.join(destProfilePath, "sessionstore.jsonlz4"),
    { decompress: true }
  );

  /**
   * These timestamps might be slightly different from one another, so we'll exclude
   * them from the comparison.
   */
  delete sessionStateCopied.session.lastUpdate;
  delete sessionStoreState.session.lastUpdate;
  Assert.deepEqual(
    sessionStateCopied,
    sessionStoreState,
    "sessionstore.jsonlz4 in the destination profile folder matches the backed up session state"
  );

  await maybeRemovePath(recoveryPath);
  await maybeRemovePath(destProfilePath);
});
