/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PreferencesBackupResource } = ChromeUtils.importESModule(
  "resource:///modules/backup/PreferencesBackupResource.sys.mjs"
);
const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);
const { SearchService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchService.sys.mjs"
);
const { SearchUtils } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchUtils.sys.mjs"
);
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

const SEARCH_PREFS_FILENAME = "search.json.mozlz4";

SearchTestUtils.init(this);

updateAppInfo({ name: "XPCShell", version: "48", platformVersion: "48" });
do_get_profile();

const FAKE_SEARCH_EXTENSION_NAME = "Some WebExtension Search Engine";
const FAKE_PRIVATE_SEARCH_EXTENSION_NAME =
  "Some Private WebExtension Search Engine";

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.separatePrivateDefault", true);
  Services.prefs.setBoolPref(
    "browser.search.separatePrivateDefault.ui.enabled",
    true
  );

  await SearchTestUtils.setRemoteSettingsConfig([
    { identifier: "engine1" },
    { identifier: "engine2" },
  ]);

  Services.prefs.setCharPref(SearchUtils.BROWSER_SEARCH_PREF + "region", "US");
  Services.locale.availableLocales = ["en-US"];
  Services.locale.requestedLocales = ["en-US"];

  await SearchService.init();

  await SearchTestUtils.installSearchExtension(
    {
      name: FAKE_SEARCH_EXTENSION_NAME,
      search_url: "https://example.com/plain",
    },
    { setAsDefault: true }
  );

  await SearchTestUtils.installSearchExtension(
    {
      name: FAKE_PRIVATE_SEARCH_EXTENSION_NAME,
      search_url: "https://example.com/",
      search_url_get_params: "private={searchTerms}",
    },
    { setAsDefaultPrivate: true }
  );

  await SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
});

/**
 * Tests that PreferencesBackupResource will correctly recompute search engine
 * verification hashes after a recovery. This is more of an integration test
 * than what already exists in test_PreferencesBackupResource.js.
 */
add_task(async function test_recover_searchEngines_verified() {
  let preferencesBackupResource = new PreferencesBackupResource();
  let recoveryPath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-recovery-test"
  );
  let destProfilePath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-test-profile"
  );

  await IOUtils.copy(
    PathUtils.join(PathUtils.profileDir, SEARCH_PREFS_FILENAME),
    PathUtils.join(recoveryPath, SEARCH_PREFS_FILENAME)
  );

  let postRecoveryEntry = await preferencesBackupResource.recover(
    { profilePath: PathUtils.profileDir },
    recoveryPath,
    destProfilePath
  );

  Assert.equal(
    postRecoveryEntry,
    null,
    "PreferencesBackupResource.recover should return null as its post recovery entry"
  );

  let originalSearchEngineSettings = await IOUtils.readJSON(
    PathUtils.join(PathUtils.profileDir, SEARCH_PREFS_FILENAME),
    { decompress: true }
  );

  let recoveredSearchEngineSettings = await IOUtils.readJSON(
    PathUtils.join(destProfilePath, SEARCH_PREFS_FILENAME),
    { decompress: true }
  );

  Assert.notEqual(
    originalSearchEngineSettings.metaData.defaultEngineIdHash,
    recoveredSearchEngineSettings.metaData.defaultEngineIdHash,
    "defaultEngineIdHash was updated."
  );
  Assert.equal(
    recoveredSearchEngineSettings.metaData.defaultEngineIdHash,
    SearchUtils.getVerificationHash(
      originalSearchEngineSettings.metaData.defaultEngineId,
      destProfilePath
    ),
    "defaultEngineIdHash was updated correctly."
  );

  Assert.notEqual(
    originalSearchEngineSettings.metaData.privateDefaultEngineIdHash,
    recoveredSearchEngineSettings.metaData.privateDefaultEngineIdHash,
    "privateDefaultEngineIdHash was updated."
  );
  Assert.equal(
    recoveredSearchEngineSettings.metaData.privateDefaultEngineIdHash,
    SearchUtils.getVerificationHash(
      originalSearchEngineSettings.metaData.privateDefaultEngineId,
      destProfilePath
    ),
    "privateDefaultEngineIdHash was updated correctly."
  );

  Assert.equal(
    originalSearchEngineSettings.engines.length,
    recoveredSearchEngineSettings.engines.length,
    "Got the same number of engines"
  );

  for (let i = 0; i < originalSearchEngineSettings.engines.length; ++i) {
    let originalEngine = originalSearchEngineSettings.engines[i];
    let recoveredEngine = recoveredSearchEngineSettings.engines[i];

    if (originalEngine._metaData.loadPathHash) {
      Assert.ok(
        recoveredEngine._metaData.loadPathHash,
        "Recovered engine also has a loadPathHash"
      );
      Assert.notEqual(
        originalEngine._metaData.loadPathHash,
        recoveredEngine._metaData.loadPathHash,
        "loadPathHash was updated."
      );
      Assert.equal(
        recoveredEngine._metaData.loadPathHash,
        SearchUtils.getVerificationHash(
          originalEngine._loadPath,
          destProfilePath
        ),
        "loadPathHash had the expected value."
      );
    } else {
      Assert.deepEqual(
        originalEngine,
        recoveredEngine,
        "Engine was not changed."
      );
    }
  }

  await maybeRemovePath(recoveryPath);
  await maybeRemovePath(destProfilePath);
});

/**
 * Tests that PreferencesBackupResource will not update the verification hashes
 * for any search engines that fail to verify for the original path.
 */
add_task(async function test_recover_searchEngines_unverified() {
  let preferencesBackupResource = new PreferencesBackupResource();
  let recoveryPath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-recovery-test"
  );
  let destProfilePath = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "PreferencesBackupResource-test-profile"
  );

  // Now let's read in the original search preferences file and break some
  // of the verification hashes...
  let searchEngineSettings = await IOUtils.readJSON(
    PathUtils.join(PathUtils.profileDir, SEARCH_PREFS_FILENAME),
    { decompress: true }
  );

  const BOGUS_HASH = "bogus hash!";

  searchEngineSettings.metaData.defaultEngineIdHash = BOGUS_HASH;
  searchEngineSettings.metaData.privateDefaultEngineIdHash = BOGUS_HASH;

  for (let engine of searchEngineSettings.engines) {
    if (engine._metaData.loadPathHash) {
      engine._metaData.loadPathHash = BOGUS_HASH;
    }
  }

  // And now let us write this data out to the recovery path.
  await IOUtils.writeJSON(
    PathUtils.join(recoveryPath, SEARCH_PREFS_FILENAME),
    searchEngineSettings,
    {
      compress: true,
    }
  );

  let postRecoveryEntry = await preferencesBackupResource.recover(
    { profilePath: PathUtils.profileDir },
    recoveryPath,
    destProfilePath
  );

  Assert.equal(
    postRecoveryEntry,
    null,
    "PreferencesBackupResource.recover should return null as its post recovery entry"
  );

  // And now let us write this data out to the recovery path.
  let recoveredSearchEngineSettings = await IOUtils.readJSON(
    PathUtils.join(destProfilePath, SEARCH_PREFS_FILENAME),
    {
      decompress: true,
    }
  );

  Assert.equal(
    recoveredSearchEngineSettings.metaData.defaultEngineIdHash,
    BOGUS_HASH,
    "Bogus defaultEngineIdHash was not changed."
  );

  Assert.equal(
    recoveredSearchEngineSettings.metaData.privateDefaultEngineIdHash,
    BOGUS_HASH,
    "Bogus privateDefaultEngineIdHash was not changed."
  );

  Assert.equal(
    searchEngineSettings.engines.length,
    recoveredSearchEngineSettings.engines.length,
    "Got the same number of engines"
  );

  for (let i = 0; i < searchEngineSettings.engines.length; ++i) {
    let originalEngine = searchEngineSettings.engines[i];
    let recoveredEngine = recoveredSearchEngineSettings.engines[i];

    if (originalEngine._metaData.loadPathHash) {
      Assert.ok(
        recoveredEngine._metaData.loadPathHash,
        "Recovered engine also has a loadPathHash"
      );
      Assert.equal(
        recoveredEngine._metaData.loadPathHash,
        BOGUS_HASH,
        "Bogus loadPathHash was not changed."
      );
    } else {
      Assert.deepEqual(
        originalEngine,
        recoveredEngine,
        "Engine was not changed."
      );
    }
  }

  await maybeRemovePath(recoveryPath);
  await maybeRemovePath(destProfilePath);
});
