/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that if the migrator does not have the permission to
 * read from the Chrome data directory, that it can request
 * permission to read from it, if the system allows.
 */

const { ChromeMigrationUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeMigrationUtils.sys.mjs"
);

const { ChromeProfileMigrator, OperaGXProfileMigrator } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/migration/ChromeProfileMigrator.sys.mjs"
  );

const { MigratorBase } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/MigratorBase.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { MockFilePicker } = ChromeUtils.importESModule(
  "resource://testing-common/MockFilePicker.sys.mjs"
);

let gTempDir;

add_setup(async () => {
  Services.prefs.setBoolPref(
    "browser.migrate.chrome.get_permissions.enabled",
    true
  );
  gTempDir = do_get_tempdir();
  await IOUtils.writeJSON(PathUtils.join(gTempDir.path, "Local State"), []);
  MockFilePicker.init();
  registerCleanupFunction(() => {
    MockFilePicker.cleanup();
  });
});

/**
 * Tests that canGetPermissions will return false if the platform does
 * not allow for folder selection in the native file picker, and returns
 * the data path otherwise.
 */
add_task(async function test_canGetPermissions() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  let migrator = new ChromeProfileMigrator();
  let canGetPermissionsStub = sandbox
    .stub(MigrationUtils, "canGetPermissionsOnPlatform")
    .resolves(false);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);

  Assert.ok(
    !(await migrator.canGetPermissions()),
    "Should not be able to get permissions."
  );

  canGetPermissionsStub.resolves(true);

  Assert.equal(
    await migrator.canGetPermissions(),
    gTempDir.path,
    "Should be able to get the permissions path."
  );

  sandbox.restore();
});

/**
 * Tests that getPermissions will show the native file picker in a
 * loop until either the user cancels or selects a folder that grants
 * read permissions to the data directory.
 */
add_task(async function test_getPermissions() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  let migrator = new ChromeProfileMigrator();
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);
  let hasPermissionsStub = sandbox
    .stub(migrator, "hasPermissions")
    .resolves(false);

  Assert.equal(
    await migrator.canGetPermissions(),
    gTempDir.path,
    "Should be able to get the permissions path."
  );

  let filePickerSeenCount = 0;

  let filePickerShownPromise = new Promise(resolve => {
    MockFilePicker.showCallback = () => {
      Assert.ok(true, "Filepicker shown.");
      MockFilePicker.useDirectory([gTempDir.path]);
      filePickerSeenCount++;
      if (filePickerSeenCount > 3) {
        Assert.ok(true, "File picker looped 3 times.");
        hasPermissionsStub.resolves(true);
        resolve();
      }
    };
  });
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  let onFilePickerShown = sinon.spy();
  Assert.ok(
    await migrator.getPermissions(null, { onFilePickerShown }),
    "Should report that we got permissions."
  );

  await filePickerShownPromise;
  Assert.equal(
    onFilePickerShown.callCount,
    filePickerSeenCount,
    "Reported each file picker."
  );

  // Make sure that the user can also hit "cancel" and that we
  // file picker loop.

  hasPermissionsStub.resolves(false);

  filePickerSeenCount = 0;
  filePickerShownPromise = new Promise(resolve => {
    MockFilePicker.showCallback = () => {
      Assert.ok(true, "Filepicker shown.");
      filePickerSeenCount++;
      Assert.equal(filePickerSeenCount, 1, "Saw the picker once.");
      resolve();
    };
  });
  MockFilePicker.returnValue = MockFilePicker.returnCancel;
  onFilePickerShown.resetHistory();
  Assert.ok(
    !(await migrator.getPermissions(null, { onFilePickerShown })),
    "Should report that we didn't get permissions."
  );
  await filePickerShownPromise;
  Assert.equal(onFilePickerShown.callCount, 1, "Reported the file picker.");

  sandbox.restore();
});

/**
 * Tests that if the native file picker chooses a different directory
 * than the one we originally asked for, that we remap attempts to
 * read profiles from that new directory. This is because Ubuntu Snaps
 * will return us paths from the native file picker that are symlinks
 * to the original directories.
 */
add_task(async function test_remapDirectories() {
  let remapDir = new FileUtils.File(
    await IOUtils.createUniqueDirectory(
      PathUtils.tempDir,
      "test-chrome-migration"
    )
  );
  let localStatePath = PathUtils.join(remapDir.path, "Local State");
  await IOUtils.writeJSON(localStatePath, []);

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  let migrator = new ChromeProfileMigrator();
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);
  let hasPermissionsStub = sandbox
    .stub(migrator, "hasPermissions")
    .resolves(false);

  Assert.equal(
    await migrator.canGetPermissions(),
    gTempDir.path,
    "Should be able to get the permissions path."
  );

  let filePickerShownPromise = new Promise(resolve => {
    MockFilePicker.showCallback = () => {
      Assert.ok(true, "Filepicker shown.");
      MockFilePicker.useDirectory([remapDir.path]);
      hasPermissionsStub.resolves(true);
      resolve();
    };
  });
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  Assert.ok(
    await migrator.getPermissions(),
    "Should report that we got permissions."
  );

  Assert.equal(
    PathUtils.normalize(await migrator.canGetPermissions()),
    PathUtils.normalize(remapDir.path),
    "Should be able to get the remapped permissions path."
  );

  await filePickerShownPromise;

  sandbox.restore();
});

/**
 * Tests that if the platform blocks reading the data directory until the
 * user selects it in the file picker, we neither read Local State nor list
 * any profiles before that selection, and that we read it afterwards.
 */
add_task(async function test_noReadsBeforeFolderSelection() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);
  let getLocalStateSpy = sandbox.spy(ChromeMigrationUtils, "getLocalState");

  let migrator = new ChromeProfileMigrator();
  Assert.ok(
    !(await migrator.hasPermissions()),
    "Should report missing permissions even though Local State is readable."
  );
  Assert.deepEqual(
    await migrator.getSourceProfiles(),
    [],
    "Should not list any profiles before the folder is selected."
  );
  Assert.equal(
    await migrator.canGetPermissions(),
    gTempDir.path,
    "Should still offer to get permissions."
  );
  Assert.ok(getLocalStateSpy.notCalled, "Should not have read Local State.");

  MockFilePicker.showCallback = () => {
    MockFilePicker.useDirectory([gTempDir.path]);
  };
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  Assert.ok(
    await migrator.getPermissions(),
    "Should report that we got permissions."
  );
  Assert.ok(
    await migrator.hasPermissions(),
    "Should report permissions after the folder was selected."
  );

  await migrator.getSourceProfiles();
  Assert.ok(
    getLocalStateSpy.calledOnce,
    "Should read Local State after the folder was selected."
  );

  sandbox.restore();
});

/**
 * Tests that if the user selects a folder that does not give access and then
 * cancels the file picker, we go back to not reading any Chrome data.
 */
add_task(async function test_noReadsAfterWrongFolderAndCancel() {
  let wrongDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "test-chrome-wrong-folder"
  );
  registerCleanupFunction(async () => {
    await IOUtils.remove(wrongDir, { recursive: true });
  });

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);

  let migrator = new ChromeProfileMigrator();
  let filePickerSeenCount = 0;
  MockFilePicker.showCallback = () => {
    filePickerSeenCount++;
    if (filePickerSeenCount == 1) {
      MockFilePicker.useDirectory([wrongDir]);
      return MockFilePicker.returnOK;
    }
    return MockFilePicker.returnCancel;
  };
  Assert.ok(
    !(await migrator.getPermissions()),
    "Should report that we didn't get permissions."
  );
  Assert.equal(filePickerSeenCount, 2, "Should have shown the picker twice.");

  // Add Local State to the selected folder, so that reading it would succeed.
  await IOUtils.writeJSON(PathUtils.join(wrongDir, "Local State"), []);
  Assert.ok(
    !(await migrator.hasPermissions()),
    "Should not read any data after the file picker was cancelled."
  );

  sandbox.restore();
});

/**
 * Tests that before the user selects the data folder, the last used date
 * comes from the modification date of Local State, without reading it, so
 * that Chrome can still be listed first in the migration wizard.
 */
add_task(async function test_lastUsedDateBeforeFolderSelection() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);
  let getLocalStateSpy = sandbox.spy(ChromeMigrationUtils, "getLocalState");

  let lastModified = new Date("2026-11-01T12:00:00Z").getTime();
  await IOUtils.setModificationTime(
    PathUtils.join(gTempDir.path, "Local State"),
    lastModified
  );

  let migrator = new ChromeProfileMigrator();
  Assert.equal(
    (await migrator.getLastUsedDate()).getTime(),
    lastModified,
    "Should use the modification date of Local State."
  );
  Assert.ok(getLocalStateSpy.notCalled, "Should not have read Local State.");

  sandbox.restore();
});

/**
 * Tests that looking for importable logins does not read any Chrome data
 * if the platform blocks reading it until the user selects its folder.
 */
add_task(async function test_noImportableLoginReadsBeforeFolderSelection() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
    ChromeMigrationUtils._importableLoginsCache = null;
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);
  sandbox
    .stub(ChromeMigrationUtils, "supportsLoginsForPlatform")
    .get(() => true);
  let getLocalStateSpy = sandbox.spy(ChromeMigrationUtils, "getLocalState");
  let getRowsSpy = sandbox.spy(MigrationUtils, "getRowsFromDBWithoutLocks");
  ChromeMigrationUtils._importableLoginsCache = null;

  Assert.ok(
    !(await ChromeMigrationUtils.getImportableLogins("https://example.com"))
      ?.length,
    "Should not find any importable logins."
  );
  Assert.ok(getLocalStateSpy.notCalled, "Should not have read Local State.");
  Assert.ok(getRowsSpy.notCalled, "Should not have read Login Data.");

  sandbox.restore();
});

/**
 * Tests that a migrator without profiles finds its resources once the user
 * has selected its folder, even if they were looked up before.
 */
add_task(async function test_resourcesAfterFolderSelection() {
  let dataDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "test-opera-gx-migration"
  );
  await IOUtils.writeJSON(PathUtils.join(dataDir, "Local State"), {});
  await IOUtils.writeJSON(PathUtils.join(dataDir, "Bookmarks"), {
    roots: {
      bookmark_bar: {
        children: [
          { type: "url", name: "Example", url: "https://example.com/" },
        ],
      },
    },
  });

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(dataDir);

  let migrator = new OperaGXProfileMigrator();
  Assert.ok(
    !(await migrator.isSourceAvailable()),
    "Should not report a source before the folder is selected."
  );
  Assert.equal(
    await migrator.getMigrateData(null),
    0,
    "Should not report any data before the folder is selected."
  );

  MockFilePicker.showCallback = () => {
    MockFilePicker.useDirectory([dataDir]);
  };
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  Assert.ok(
    await migrator.getPermissions(),
    "Should report that we got permissions."
  );

  Assert.ok(
    await migrator.isSourceAvailable(),
    "Should report a source after the folder was selected."
  );
  Assert.ok(
    (await migrator.getMigrateData(null)) &
      MigrationUtils.resourceTypes.BOOKMARKS,
    "Should report bookmarks after the folder was selected."
  );

  sandbox.restore();
  await IOUtils.remove(dataDir, { recursive: true });
});

/**
 * Tests that the file picker opens in the data folder, so that it can be
 * selected right away.
 */
add_task(async function test_filePickerStartsInDataFolder() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);
  let migrator = new ChromeProfileMigrator();
  sandbox.stub(migrator, "hasPermissions").resolves(false);

  let displayDirectoryPath;
  MockFilePicker.showCallback = () => {
    displayDirectoryPath = MockFilePicker.displayDirectory?.path;
  };
  MockFilePicker.returnValue = MockFilePicker.returnCancel;
  Assert.ok(
    !(await migrator.getPermissions()),
    "Should report that we didn't get permissions."
  );
  Assert.equal(
    displayDirectoryPath,
    gTempDir.path,
    "File picker should open in the data folder."
  );

  sandbox.restore();
});

/**
 * Tests that if the user first selects a wrong folder and then the original
 * data folder, we read from the original data folder again instead of the
 * wrong one.
 */
add_task(async function test_selectOriginalFolderAfterWrongFolder() {
  let wrongDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "test-chrome-wrong-folder"
  );
  registerCleanupFunction(async () => {
    await IOUtils.remove(wrongDir, { recursive: true });
  });

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(gTempDir.path);

  let migrator = new ChromeProfileMigrator();
  let filePickerSeenCount = 0;
  MockFilePicker.showCallback = () => {
    filePickerSeenCount++;
    if (filePickerSeenCount == 1) {
      MockFilePicker.useDirectory([wrongDir]);
      return MockFilePicker.returnOK;
    }
    if (filePickerSeenCount == 2) {
      MockFilePicker.useDirectory([gTempDir.path]);
      return MockFilePicker.returnOK;
    }
    return MockFilePicker.returnCancel;
  };
  Assert.ok(
    await migrator.getPermissions(),
    "Should report that we got permissions."
  );
  Assert.equal(filePickerSeenCount, 2, "Should have shown the picker twice.");
  Assert.equal(
    await migrator.canGetPermissions(),
    gTempDir.path,
    "Should read from the original data folder."
  );

  sandbox.restore();
});
