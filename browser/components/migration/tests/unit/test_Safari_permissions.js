/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that if the migrator does not have the permission to
 * read from the Safari data directory, that it can request
 * permission to read from it, if the system allows.
 */

const { SafariProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/SafariProfileMigrator.sys.mjs"
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

let gDataDir;

add_setup(async () => {
  let tempDir = do_get_tempdir();
  gDataDir = PathUtils.join(tempDir.path, "Safari");
  await IOUtils.makeDirectory(gDataDir);

  registerFakePath("ULibDir", tempDir);

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

  let migrator = new SafariProfileMigrator();
  // Not being able to get a folder picker is not a problem on macOS, but
  // we'll test that case anyways.
  let canGetPermissionsStub = sandbox
    .stub(MigrationUtils, "canGetPermissionsOnPlatform")
    .resolves(false);

  Assert.ok(
    !(await migrator.canGetPermissions()),
    "Should not be able to get permissions."
  );

  canGetPermissionsStub.resolves(true);

  Assert.equal(
    await migrator.canGetPermissions(),
    gDataDir,
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

  let migrator = new SafariProfileMigrator();
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  let hasPermissionsStub = sandbox
    .stub(migrator, "hasPermissions")
    .resolves(false);

  Assert.equal(
    await migrator.canGetPermissions(),
    gDataDir,
    "Should be able to get the permissions path."
  );

  let filePickerSeenCount = 0;

  let filePickerShownPromise = new Promise(resolve => {
    MockFilePicker.showCallback = () => {
      Assert.ok(true, "Filepicker shown.");
      MockFilePicker.useDirectory([gDataDir]);
      filePickerSeenCount++;
      if (filePickerSeenCount > 3) {
        Assert.ok(true, "File picker looped 3 times.");
        hasPermissionsStub.resolves(true);
        resolve();
      }
    };
  });
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  // This is a little awkward, but we need to ensure that the
  // filePickerShownPromise resolves first before we await
  // the getPermissionsPromise in order to get the correct
  // filePickerSeenCount.
  let onFilePickerShown = sinon.spy();
  let getPermissionsPromise = migrator.getPermissions(null, {
    onFilePickerShown,
  });
  await filePickerShownPromise;
  Assert.ok(
    await getPermissionsPromise,
    "Should report that we got permissions."
  );
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
 * Tests that the file picker opens in the Safari folder, so that it can be
 * selected right away.
 */
add_task(async function test_filePickerStartsInSafariFolder() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  let migrator = new SafariProfileMigrator();
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
    gDataDir,
    "File picker should open in the Safari folder."
  );

  sandbox.restore();
});

/**
 * Tests that if the platform blocks reading the Safari folder until the
 * user selects it in the file picker, we don't read anything in it before
 * that selection, and that history is still offered for import.
 */
add_task(async function test_noReadsBeforeFolderSelection() {
  await IOUtils.writeUTF8(PathUtils.join(gDataDir, "History.db"), "history");
  await IOUtils.writeUTF8(
    PathUtils.join(gDataDir, "Bookmarks.plist"),
    "bookmarks"
  );

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  let getRowsSpy = sandbox.spy(MigrationUtils, "getRowsFromDBWithoutLocks");

  let migrator = new SafariProfileMigrator();
  Assert.ok(
    !(await migrator.hasPermissions()),
    "Should report missing permissions even though the files are readable."
  );

  let resources = await migrator.getResources();
  Assert.ok(
    resources.some(r => r.type == MigrationUtils.resourceTypes.HISTORY),
    "Should still offer history before the folder is selected."
  );
  Assert.ok(getRowsSpy.notCalled, "Should not have queried History.db.");

  MockFilePicker.showCallback = () => {
    MockFilePicker.useDirectory([gDataDir]);
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

  sandbox.restore();
  await IOUtils.remove(PathUtils.join(gDataDir, "History.db"));
  await IOUtils.remove(PathUtils.join(gDataDir, "Bookmarks.plist"));
});

/**
 * Tests that if the user selects a folder that does not give access to the
 * Safari folder and then cancels the file picker, we go back to not reading
 * anything in it.
 */
add_task(async function test_noReadsAfterWrongFolderAndCancel() {
  let historyPath = PathUtils.join(gDataDir, "History.db");
  await IOUtils.writeUTF8(historyPath, "history");
  // Make reading History.db fail, like it does until the user selects the
  // Safari folder.
  await IOUtils.setPermissions(historyPath, 0o000);
  registerCleanupFunction(async () => {
    await IOUtils.remove(historyPath);
  });

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);

  let migrator = new SafariProfileMigrator();
  let filePickerSeenCount = 0;
  MockFilePicker.showCallback = () => {
    filePickerSeenCount++;
    if (filePickerSeenCount == 1) {
      MockFilePicker.useDirectory([PathUtils.tempDir]);
      return MockFilePicker.returnOK;
    }
    return MockFilePicker.returnCancel;
  };
  Assert.ok(
    !(await migrator.getPermissions()),
    "Should report that we didn't get permissions."
  );
  Assert.equal(filePickerSeenCount, 2, "Should have shown the picker twice.");

  await IOUtils.setPermissions(historyPath, 0o644);
  Assert.ok(
    !(await migrator.hasPermissions()),
    "Should not read anything after the file picker was cancelled."
  );

  sandbox.restore();
});
