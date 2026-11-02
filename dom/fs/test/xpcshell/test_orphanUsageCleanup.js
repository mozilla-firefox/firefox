/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Regression test for FileIds/Usages rows that nothing references anymore:
// rows whose handle was set to NULL because the on-disk blob could not be
// removed, and rows of temporary files whose writable stream never closed
// because of a crash. FileSystemDataManager removes every file id without a
// MainFiles row when it opens the database, and the Usages rows cascade.
//
// This test directly manipulates metadata.sqlite and therefore cannot use the
// shared module loader; it runs as a standalone privileged xpcshell test.

const { FileUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/FileUtils.sys.mjs"
);
const { PrincipalUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/PrincipalUtils.sys.mjs"
);
const { clearStoragesForOrigin, getCachedUsageForOrigin, resetStorage } =
  ChromeUtils.importESModule(
    "resource://testing-common/dom/quota/test/modules/StorageUtils.sys.mjs"
  );

const ORIGIN = "https://example.com";
const DB_PATH = "storage/default/https+++example.com/fs/metadata.sqlite";
// Usage of the orphaned Usages row in the metadata.sqlite attached to
// bug 2034086 comment 0.
const PHANTOM_USAGE = 9674162224;

async function evalInOPFSSandbox(principal, script) {
  return new Promise(function (resolve, reject) {
    const sandbox = new Cu.Sandbox(principal, {
      wantGlobalProperties: ["storage"],
      forceSecureContext: true,
    });
    sandbox.resolve = resolve;
    sandbox.reject = reject;
    Cu.evalInSandbox(script, sandbox);
  });
}

function openOPFS(principal) {
  return evalInOPFSSandbox(
    principal,
    `storage.getDirectory().then(resolve, reject);`
  );
}

function createFile(principal, name) {
  return evalInOPFSSandbox(
    principal,
    `storage
      .getDirectory()
      .then(root => root.getFileHandle(${JSON.stringify(name)}, { create: true }))
      .then(resolve, reject);`
  );
}

function queryInt(db, sql) {
  const stmt = db.createStatement(sql);
  try {
    Assert.ok(stmt.executeStep(), `Query has a result: ${sql}`);
    return stmt.getInt64(0);
  } finally {
    stmt.finalize();
  }
}

add_task(async function test_orphan_usages_self_heal() {
  const principal = PrincipalUtils.createPrincipal(ORIGIN);

  info("Initializing OPFS origin and creating a file");
  await createFile(principal, "kept.txt");

  info("Closing the origin by resetting storage");
  await resetStorage();

  {
    info("Injecting leftover FileIds/Usages rows");
    const dbFile = FileUtils.getFile(DB_PATH);
    Assert.ok(dbFile.exists(), "metadata.sqlite exists after OPFS use");

    const db = Services.storage.openUnsharedDatabase(dbFile);

    // A fake 32-byte fileId with no Files/Entries parent, the shape left
    // behind when disk removal fails and handle is set to NULL.
    const unlinkedId =
      "X'DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEF'";
    db.executeSimpleSQL(
      `INSERT INTO FileIds (fileId, handle) VALUES (${unlinkedId}, NULL)`
    );
    db.executeSimpleSQL(
      `INSERT INTO Usages (handle, usage, tracked) VALUES (${unlinkedId}, 9000000, 0)`
    );

    // A temporary file of a writable stream whose close never happened: the
    // fileId maps to an existing file but is not its main file.
    const temporaryId =
      "X'CAFEBABECAFEBABECAFEBABECAFEBABECAFEBABECAFEBABECAFEBABECAFEBABE'";
    db.executeSimpleSQL(
      `INSERT INTO FileIds (fileId, handle) ` +
        `SELECT ${temporaryId}, handle FROM Files LIMIT 1`
    );
    db.executeSimpleSQL(
      `INSERT INTO Usages (handle, usage, tracked) VALUES (${temporaryId}, 5000000, 1)`
    );

    const injected = queryInt(
      db,
      "SELECT count(*) FROM FileIds WHERE fileId NOT IN " +
        "( SELECT fileId FROM MainFiles WHERE fileId IS NOT NULL )"
    );
    db.close();

    Assert.equal(injected, 2, "Both leftover FileIds rows were injected");
  }

  info("Reopening OPFS origin, opening the database sweeps the leftovers");
  await openOPFS(principal);

  info("Closing origin again to read back DB cleanly");
  await resetStorage();

  {
    info("Verifying leftover rows were removed");
    const dbFile = FileUtils.getFile(DB_PATH);
    const db = Services.storage.openUnsharedDatabase(dbFile);

    const remaining = queryInt(
      db,
      "SELECT count(*) FROM FileIds WHERE fileId NOT IN " +
        "( SELECT fileId FROM MainFiles WHERE fileId IS NOT NULL )"
    );
    const files = queryInt(db, "SELECT count(*) FROM Files");
    const totalUsage = queryInt(
      db,
      "SELECT coalesce(sum(usage), 0) FROM Usages"
    );

    db.close();

    Assert.equal(remaining, 0, "No FileIds rows without a main file remain");
    Assert.equal(files, 1, "The real file is still there");
    Assert.equal(totalUsage, 0, "Usages of the leftovers were cascade-deleted");
  }
});

// Reproduces the database attached to bug 2034086 comment 0: a Usages row
// whose handle does not appear in FileIds at all.  This happened when the
// entry/file metadata was cascade-deleted through DeleteEntry but the Usages
// row survived because foreign key enforcement was off.
add_task(async function test_usages_without_fileids_self_heal() {
  const principal = PrincipalUtils.createPrincipal(ORIGIN);

  info("Clearing the origin so this task does not depend on the previous one");
  await clearStoragesForOrigin(principal);

  info("Initializing OPFS origin and creating a file");
  await createFile(principal, "kept.txt");

  info("Closing the origin by resetting storage");
  await resetStorage();

  {
    info("Injecting a bare Usages row with no FileIds parent");
    const dbFile = FileUtils.getFile(DB_PATH);
    Assert.ok(dbFile.exists(), "metadata.sqlite exists after OPFS use");

    const db = Services.storage.openUnsharedDatabase(dbFile);

    // A 32-byte handle that exists only in Usages, not in FileIds.
    const phantomHandle =
      "X'8FA29A5FF357A0EE3EF1D9F481742462AED65C1F841E32E7ED9FB0A79E3E4881'";
    db.executeSimpleSQL(
      `INSERT INTO Usages (handle, usage, tracked) ` +
        `VALUES (${phantomHandle}, ${PHANTOM_USAGE}, 0)`
    );

    const orphanedUsages = queryInt(
      db,
      "SELECT count(*) FROM Usages WHERE handle NOT IN " +
        "( SELECT fileId FROM FileIds )"
    );
    db.close();

    Assert.equal(orphanedUsages, 1, "Bare Usages row was injected");
  }

  info("Reopening OPFS origin, the sweep removes the bare Usages row");
  await openOPFS(principal);

  const cachedUsage = await getCachedUsageForOrigin(principal);
  Assert.less(
    cachedUsage,
    PHANTOM_USAGE,
    "Cached quota usage no longer includes the phantom row"
  );

  info("Closing origin again to read back DB cleanly");
  await resetStorage();

  {
    info("Verifying bare Usages row was removed");
    const dbFile = FileUtils.getFile(DB_PATH);
    const db = Services.storage.openUnsharedDatabase(dbFile);

    const orphanedUsages = queryInt(
      db,
      "SELECT count(*) FROM Usages WHERE handle NOT IN " +
        "( SELECT fileId FROM FileIds )"
    );
    const files = queryInt(db, "SELECT count(*) FROM Files");
    const totalUsage = queryInt(
      db,
      "SELECT coalesce(sum(usage), 0) FROM Usages"
    );

    db.close();

    Assert.equal(orphanedUsages, 0, "No Usages rows without a FileIds parent");
    Assert.equal(files, 1, "The real file is still there");
    Assert.equal(totalUsage, 0, "Phantom usage was removed");
  }
});
