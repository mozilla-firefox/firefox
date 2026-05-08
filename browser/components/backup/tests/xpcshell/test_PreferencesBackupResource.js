/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PreferencesBackupResource } = ChromeUtils.importESModule(
  "resource:///modules/backup/PreferencesBackupResource.sys.mjs"
);
const { SearchUtils } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchUtils.sys.mjs"
);

const WALLPAPER_TYPE_PREF =
  "browser.newtabpage.activity-stream.newtabWallpapers.wallpaper";
const CUSTOM_WALLPAPER_UUID_PREF =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.uuid";
const CUSTOM_WALLPAPER_FOLDER = "wallpaper";
const FAKE_CUSTOM_WALLPAPER_UUID = "decafbad-0cd1-0cd2-0cd3-decafbad1000";

/**
 * Test that the measure method correctly collects the disk-sizes of things that
 * the PreferencesBackupResource is meant to back up.
 */
add_task(async function test_measure() {
  Services.fog.testResetFOG();

  const EXPECTED_PREFERENCES_KILOBYTES_SIZE = 56;
  const tempDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-measure-test"
  );
  const mockFiles = [
    { path: "prefs.js", sizeInKB: 20 },
    { path: "xulstore.json", sizeInKB: 1 },
    { path: "containers.json", sizeInKB: 1 },
    { path: "customKeys.json", sizeInKB: 1 },
    { path: "handlers.json", sizeInKB: 1 },
    { path: "search.json.mozlz4", sizeInKB: 1 },
    { path: "user.js", sizeInKB: 2 },
    { path: ["chrome", "userChrome.css"], sizeInKB: 5 },
    { path: ["chrome", "userContent.css"], sizeInKB: 5 },
    { path: ["chrome", "css", "mockStyles.css"], sizeInKB: 5 },
    {
      path: [CUSTOM_WALLPAPER_FOLDER, FAKE_CUSTOM_WALLPAPER_UUID],
      sizeInKB: 5,
    },
  ];

  await createTestFiles(tempDir, mockFiles);

  let preferencesBackupResource = new PreferencesBackupResource();

  await preferencesBackupResource.measure(tempDir);

  let measurement = Glean.browserBackup.preferencesSize.testGetValue();
  let scalars = TelemetryTestUtils.getProcessScalars("parent", false, false);

  TelemetryTestUtils.assertScalar(
    scalars,
    "browser.backup.preferences_size",
    measurement,
    "Glean and telemetry measurements for preferences data should be equal"
  );
  Assert.equal(
    measurement,
    EXPECTED_PREFERENCES_KILOBYTES_SIZE,
    "Should have collected the correct glean measurement for preferences files"
  );

  await maybeRemovePath(tempDir);
});

/**
 * Test that the backup method correctly copies items from the profile directory
 * into the staging directory.
 */
add_task(async function test_backup() {
  let sandbox = sinon.createSandbox();

  let preferencesBackupResource = new PreferencesBackupResource();
  let sourcePath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-source-test"
  );
  let stagingPath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-staging-test"
  );

  const simpleCopyFiles = [
    { path: "xulstore.json" },
    { path: "containers.json" },
    { path: "customKeys.json" },
    { path: "handlers.json" },
    { path: "search.json.mozlz4" },
    { path: "user.js" },
    { path: ["chrome", "userChrome.css"] },
    { path: ["chrome", "userContent.css"] },
    { path: ["chrome", "childFolder", "someOtherStylesheet.css"] },
    {
      path: [CUSTOM_WALLPAPER_FOLDER, FAKE_CUSTOM_WALLPAPER_UUID],
    },
  ];
  await createTestFiles(sourcePath, simpleCopyFiles);

  const skippedCopyFiles = [
    // We should not back this one up, since the customWallpaper.uuid pref will
    // not be set to it.
    {
      path: [CUSTOM_WALLPAPER_FOLDER, "some-other-file"],
    },
  ];
  await createTestFiles(sourcePath, skippedCopyFiles);

  Services.prefs.setStringPref(WALLPAPER_TYPE_PREF, "custom");
  Services.prefs.setStringPref(
    CUSTOM_WALLPAPER_UUID_PREF,
    FAKE_CUSTOM_WALLPAPER_UUID
  );

  // We have no need to test that Sqlite.sys.mjs's backup method is working -
  // this is something that is tested in Sqlite's own tests. We can just make
  // sure that it's being called using sinon. Unfortunately, we cannot do the
  // same thing with IOUtils.copy, as its methods are not stubbable.
  let fakeConnection = {
    backup: sandbox.stub().resolves(true),
    close: sandbox.stub().resolves(true),
  };
  sandbox.stub(Sqlite, "openConnection").returns(fakeConnection);

  let manifestEntry = await preferencesBackupResource.backup(
    stagingPath,
    sourcePath
  );
  Assert.deepEqual(
    manifestEntry,
    { profileDirName: PathUtils.filename(sourcePath) },
    "PreferencesBackupResource.backup should return the profile directory name " +
      "in its ManifestEntry"
  );

  await assertFilesExist(stagingPath, simpleCopyFiles);
  await assertFilesDoNotExist(stagingPath, skippedCopyFiles);

  Assert.ok(
    fakeConnection.backup.notCalled,
    "No sqlite connections should have been made"
  );

  // And we'll make sure that preferences were properly written out.
  Assert.ok(
    await IOUtils.exists(PathUtils.join(stagingPath, "prefs.js")),
    "prefs.js should exist in the staging folder"
  );

  await maybeRemovePath(stagingPath);
  await maybeRemovePath(sourcePath);

  sandbox.restore();
});

/**
 * Check that prefs.js has "browser.backup.profile-restoration-date".  Due to
 * concerns over potential time skips in automation, we only check that the
 * timestamp is not more than a week before/after now (we would expect the
 * difference to be more like a few milliseconds).
 *
 * @param {string} prefsJsPath
 */
async function checkPrefsJsHasValidRecoveryTime(prefsJsPath) {
  Assert.equal(
    Services.prefs.getPrefType("browser.backup.profile-restoration-date"),
    Services.prefs.PREF_INVALID,
    "Restoration pref not set since current profile was not restored"
  );

  // NB: The non-profile-restoration-date part of the prefs file is junk made
  // by `createTestFiles`.  We don't care about that here.
  const contents = await IOUtils.readUTF8(prefsJsPath);
  const dateRegex =
    /pref\("browser\.backup\.profile-restoration-date", (\d+)\);/;
  let restoreDate = contents.match(dateRegex);
  Assert.equal(restoreDate.length, 2, "found the restoration date");

  const kOneWeekAgoInSec =
    60 /* sec/min */ * 60 /* min/hr */ * 24 /* hr/day */ * 7; /* day/wk */
  const nowInSeconds = Math.round(Date.now() / 1000);
  Assert.lessOrEqual(
    Math.abs(nowInSeconds - Number(restoreDate[1])),
    kOneWeekAgoInSec,
    "timestamp was within one week of now"
  );
}

/**
 * Test that the recover method correctly copies items from the recovery
 * directory into the destination profile directory.
 */
add_task(async function test_recover() {
  let sandbox = sinon.createSandbox();
  let preferencesBackupResource = new PreferencesBackupResource();
  let recoveryPath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-recovery-test"
  );
  let destProfilePath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-test-profile"
  );

  const simpleCopyFiles = [
    { path: "prefs.js" },
    { path: "xulstore.json" },
    { path: "containers.json" },
    { path: "customKeys.json" },
    { path: "handlers.json" },
    { path: "user.js" },
    { path: ["chrome", "userChrome.css"] },
    { path: ["chrome", "userContent.css"] },
    { path: ["chrome", "childFolder", "someOtherStylesheet.css"] },
    { path: [CUSTOM_WALLPAPER_FOLDER, FAKE_CUSTOM_WALLPAPER_UUID] },
  ];
  await createTestFiles(recoveryPath, simpleCopyFiles);

  // We'll now hand-prepare enough of a search.json.mozlz4 file that we can
  // ensure that PreferencesBackupResource knows how to update the
  // verification hashes for non-default engines.
  const TEST_SEARCH_ENGINE_LOAD_PATH = "some/path/on/disk";
  const TEST_SEARCH_ENGINE_LOAD_PATH_HASH = "some pre-existing hash";
  const TEST_DEFAULT_ENGINE_ID = "bugle";
  const TEST_DEFAULT_ENGINE_ID_HASH = "default engine original hash";
  const TEST_PRIVATE_DEFAULT_ENGINE_ID = "goose";
  const TEST_PRIVATE_DEFAULT_ENGINE_ID_HASH =
    "private default engine original hash";

  let fakeSearchPrefs = {
    metaData: {
      defaultEngineId: TEST_DEFAULT_ENGINE_ID,
      defaultEngineIdHash: TEST_DEFAULT_ENGINE_ID_HASH,
      privateDefaultEngineId: TEST_PRIVATE_DEFAULT_ENGINE_ID,
      privateDefaultEngineIdHash: TEST_PRIVATE_DEFAULT_ENGINE_ID_HASH,
    },
    engines: [
      {
        _loadPath: TEST_SEARCH_ENGINE_LOAD_PATH,
        _metaData: {
          loadPathHash: TEST_SEARCH_ENGINE_LOAD_PATH_HASH,
        },
      },
    ],
  };

  const SEARCH_PREFS_FILENAME = "search.json.mozlz4";
  await IOUtils.writeJSON(
    PathUtils.join(recoveryPath, SEARCH_PREFS_FILENAME),
    fakeSearchPrefs,
    {
      compress: true,
    }
  );

  const EXPECTED_HASH = "this is some newly generated hash";
  sandbox
    .stub(SearchUtils, "getVerificationHash")
    .onCall(0)
    .returns(TEST_SEARCH_ENGINE_LOAD_PATH_HASH)
    .onCall(1)
    .returns(EXPECTED_HASH)
    .onCall(2)
    .returns(TEST_DEFAULT_ENGINE_ID_HASH)
    .onCall(3)
    .returns(EXPECTED_HASH)
    .onCall(4)
    .returns(TEST_PRIVATE_DEFAULT_ENGINE_ID_HASH)
    .onCall(5)
    .returns(EXPECTED_HASH);

  const PRETEND_ORIGINAL_DIR_NAME = "some-profile-dir";

  // The backup method is expected to have returned a null ManifestEntry
  let postRecoveryEntry = await preferencesBackupResource.recover(
    { profileDirName: PRETEND_ORIGINAL_DIR_NAME },
    recoveryPath,
    destProfilePath
  );
  Assert.equal(
    postRecoveryEntry,
    null,
    "PreferencesBackupResource.recover should return null as its post recovery entry"
  );

  await assertFilesExist(destProfilePath, simpleCopyFiles);
  await checkPrefsJsHasValidRecoveryTime(
    PathUtils.join(destProfilePath, "prefs.js")
  );

  // Now ensure that the verification was properly recomputed. We should
  // Have called getVerificationHash 6 times - twice each for:
  //
  // - The single engine in the engines list
  // - The defaultEngineId
  // - The privateDefaultEngineId
  //
  // The first call is to verify the hash against the original profile path,
  // and the second call is to generate the hash for the new profile path.
  Assert.equal(
    SearchUtils.getVerificationHash.callCount,
    6,
    "SearchUtils.getVerificationHash was called the right number of times."
  );
  let destDirName = PathUtils.filename(destProfilePath);

  Assert.ok(
    SearchUtils.getVerificationHash
      .getCall(0)
      .calledWith(TEST_SEARCH_ENGINE_LOAD_PATH, PRETEND_ORIGINAL_DIR_NAME),
    "SearchUtils.getVerificationHash first call called with the right arguments."
  );
  Assert.ok(
    SearchUtils.getVerificationHash
      .getCall(1)
      .calledWith(TEST_SEARCH_ENGINE_LOAD_PATH, destDirName),
    "SearchUtils.getVerificationHash second call called with the right arguments."
  );
  Assert.ok(
    SearchUtils.getVerificationHash
      .getCall(2)
      .calledWith(TEST_DEFAULT_ENGINE_ID, PRETEND_ORIGINAL_DIR_NAME),
    "SearchUtils.getVerificationHash third call called with the right arguments."
  );
  Assert.ok(
    SearchUtils.getVerificationHash
      .getCall(3)
      .calledWith(TEST_DEFAULT_ENGINE_ID, destDirName),
    "SearchUtils.getVerificationHash fourth call called with the right arguments."
  );
  Assert.ok(
    SearchUtils.getVerificationHash
      .getCall(4)
      .calledWith(TEST_PRIVATE_DEFAULT_ENGINE_ID, PRETEND_ORIGINAL_DIR_NAME),
    "SearchUtils.getVerificationHash fifth call called with the right arguments."
  );
  Assert.ok(
    SearchUtils.getVerificationHash
      .getCall(5)
      .calledWith(TEST_PRIVATE_DEFAULT_ENGINE_ID, destDirName),
    "SearchUtils.getVerificationHash sixth call called with the right arguments."
  );

  let recoveredSearchPrefs = await IOUtils.readJSON(
    PathUtils.join(destProfilePath, SEARCH_PREFS_FILENAME),
    { decompress: true }
  );
  Assert.equal(
    recoveredSearchPrefs.engines.length,
    1,
    "Should still have 1 search engine"
  );
  Assert.equal(
    recoveredSearchPrefs.engines[0]._metaData.loadPathHash,
    EXPECTED_HASH,
    "The expected hash was written for the single engine."
  );
  Assert.equal(
    recoveredSearchPrefs.metaData.defaultEngineIdHash,
    EXPECTED_HASH,
    "The expected hash was written for the default engine."
  );
  Assert.equal(
    recoveredSearchPrefs.metaData.privateDefaultEngineIdHash,
    EXPECTED_HASH,
    "The expected hash was written for the private default engine."
  );

  await maybeRemovePath(recoveryPath);
  await maybeRemovePath(destProfilePath);
  sandbox.restore();
});

/**
 * Test that getPrefsFromBuffer correctly parses pref values from
 * prefs.js file content.
 */
add_task(async function test_getPrefsFromBuffer() {
  const mockPrefsContent = `// Mozilla User Preferences
user_pref("test.boolean.enabled", true);
user_pref("test.boolean.disabled", false);
user_pref("test.string.value", "hello world");
user_pref("test.number.value", 42);
`;
  const encoder = new TextEncoder();
  const mockPrefsBuffer = encoder.encode(mockPrefsContent);

  const allPrefs =
    PreferencesBackupResource.getPrefsFromBuffer(mockPrefsBuffer);

  Assert.strictEqual(
    allPrefs.get("test.boolean.enabled"),
    true,
    "Should correctly parse boolean true"
  );

  Assert.strictEqual(
    allPrefs.get("test.boolean.disabled"),
    false,
    "Should correctly parse boolean false"
  );

  Assert.strictEqual(
    allPrefs.get("test.string.value"),
    "hello world",
    "Should correctly parse string value"
  );

  Assert.strictEqual(
    allPrefs.get("test.number.value"),
    42,
    "Should correctly parse number value"
  );

  Assert.strictEqual(
    allPrefs.has("nonexistent.pref"),
    false,
    "Should not have nonexistent pref in map"
  );

  const filteredPrefs = PreferencesBackupResource.getPrefsFromBuffer(
    mockPrefsBuffer,
    ["test.boolean.enabled", "test.number.value"]
  );

  Assert.strictEqual(
    filteredPrefs.size,
    2,
    "Should only have 2 prefs when filtering"
  );

  Assert.strictEqual(
    filteredPrefs.get("test.boolean.enabled"),
    true,
    "Should have filtered pref"
  );

  Assert.strictEqual(
    filteredPrefs.get("test.number.value"),
    42,
    "Should have filtered pref"
  );

  Assert.strictEqual(
    filteredPrefs.has("test.string.value"),
    false,
    "Should not have non-filtered pref"
  );
});
