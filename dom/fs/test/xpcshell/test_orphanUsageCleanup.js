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
const { QuotaUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/QuotaUtils.sys.mjs"
);
const { clearStoragesForOrigin, getCachedUsageForOrigin, resetStorage } =
  ChromeUtils.importESModule(
    "resource://testing-common/dom/quota/test/modules/StorageUtils.sys.mjs"
  );

const ORIGIN = "https://example.com";
const DB_PATH = "storage/default/https+++example.com/fs/metadata.sqlite";
// Usage recorded in the metadata.sqlite attached to bug 2034086 comment 0.
const PHANTOM_USAGE = 9_674_162_224;

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
      `INSERT INTO Usages (handle, usage, tracked) VALUES (${unlinkedId}, ${PHANTOM_USAGE}, 0)`
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

  await QuotaUtils.requestFinished(Services.qms.init());
  await QuotaUtils.requestFinished(Services.qms.initTemporaryStorage());
  Assert.ok(
    FileUtils.getFile(DB_PATH).exists(),
    "The origin survives temporary storage initialization"
  );

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

// Bug 2034086 comment 0 reported stale usage after an OPFS file disappeared.
// We first modeled it as a bare Usages row left behind when foreign key
// cascades did not run. The attached database actually retains its FileIds
// row with a NULL handle; the test above covers that state. This test keeps
// the bare-row case as a separate invalid state. On Android, the large stale
// usage can cause quota to evict the origin during temporary storage
// initialization, before OPFS opens and cleans up the row.
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

  await QuotaUtils.requestFinished(Services.qms.init());
  await QuotaUtils.requestFinished(Services.qms.initTemporaryStorage());
  Assert.ok(
    FileUtils.getFile(DB_PATH).exists(),
    "The origin survives temporary storage initialization"
  );
  {
    const db = Services.storage.openUnsharedDatabase(
      FileUtils.getFile(DB_PATH)
    );
    Assert.equal(
      queryInt(db, "SELECT count(*) FROM Files"),
      1,
      "The real file survives temporary storage initialization"
    );
    db.close();
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

async function testOrphanCleanup(cleanupOnOpen) {
  const principal = PrincipalUtils.createPrincipal(ORIGIN);
  await clearStoragesForOrigin(principal);
  await createFile(principal, "kept.txt");
  await evalInOPFSSandbox(
    principal,
    `(async () => {
      const root = await storage.getDirectory();
      const handle = await root.getFileHandle("kept.txt");
      const writable = await handle.createWritable();
      await writable.write("keep this data");
      await writable.close();
    })().then(resolve, reject);`
  );
  await resetStorage();

  // These are the base32 filenames for the corresponding 32-byte file IDs.
  const candidates = [
    { hex: "00".repeat(32), name: "A".repeat(52), hasFileId: true },
    {
      hex: "11".repeat(32),
      name: "CEIRCEIRCEIRCEIRCEIRCEIRCEIRCEIRCEIRCEIRCEIRCEIRCEIQ",
      hasFileId: true,
    },
    { hex: "FF".repeat(32), name: "7".repeat(51) + "Q", hasFileId: false },
  ];
  const dbFile = FileUtils.getFile(DB_PATH);
  {
    const db = Services.storage.openUnsharedDatabase(dbFile);
    for (const [index, candidate] of candidates.entries()) {
      const directory = dbFile.parent.clone();
      directory.append(candidate.name.slice(0, 2));
      await IOUtils.makeDirectory(directory.path);
      const file = directory.clone();
      file.append(candidate.name);
      candidate.file = file;
      if (index === 0 || cleanupOnOpen) {
        // A directory at the blob path makes RemoveFile fail on every platform.
        await IOUtils.makeDirectory(file.path);
      } else {
        await IOUtils.writeUTF8(file.path, "orphan");
      }
      if (candidate.hasFileId) {
        db.executeSimpleSQL(
          `INSERT INTO FileIds VALUES (X'${candidate.hex}', NULL)`
        );
      }
      db.executeSimpleSQL(
        `INSERT INTO Usages VALUES (X'${candidate.hex}', 6, 0)`
      );
    }
    db.close();
  }

  await QuotaUtils.requestFinished(Services.qms.init());
  await QuotaUtils.requestFinished(Services.qms.initTemporaryStorage());
  await QuotaUtils.requestFinished(
    Services.qms.initializeTemporaryOrigin("default", principal)
  );
  if (cleanupOnOpen) {
    await resetStorage();
    Services.prefs.setBoolPref("dom.quotaManager.loadQuotaFromCache", true);
    Services.prefs.setBoolPref(
      "dom.quotaManager.loadQuotaFromSecondaryCache",
      true
    );
    await QuotaUtils.requestFinished(Services.qms.init());
    await QuotaUtils.requestFinished(Services.qms.initTemporaryStorage());
    await QuotaUtils.requestFinished(
      Services.qms.initializeTemporaryOrigin("default", principal)
    );
    for (const candidate of candidates) {
      Assert.ok(
        candidate.file.exists(),
        "Quota initialization retains blocked candidates"
      );
    }
    for (const candidate of candidates.slice(1)) {
      candidate.file.remove(false);
      await IOUtils.writeUTF8(candidate.file.path, "orphan");
    }
    const usageBefore = await getCachedUsageForOrigin(principal);
    await openOPFS(principal);
    Assert.equal(
      usageBefore - (await getCachedUsageForOrigin(principal)),
      12,
      "Database open releases only the successfully removed blobs' usage"
    );
  }
  {
    const db = Services.storage.openUnsharedDatabase(dbFile);
    for (const [index, candidate] of candidates.entries()) {
      const expected = index === 0 ? 1 : 0;
      Assert.equal(candidate.file.exists(), Boolean(expected));
      Assert.equal(
        queryInt(
          db,
          `SELECT count(*) FROM FileIds WHERE fileId = X'${candidate.hex}'`
        ),
        expected,
        "Only the failed removal retains its FileIds row"
      );
      Assert.equal(
        queryInt(
          db,
          `SELECT coalesce(sum(usage), 0) FROM Usages WHERE handle = X'${candidate.hex}'`
        ),
        expected * 6,
        "Only the failed removal retains its usage"
      );
    }
    Assert.equal(queryInt(db, "SELECT count(*) FROM Files"), 1);
    db.close();
  }

  await resetStorage();
  const failed = candidates[0];
  if (cleanupOnOpen) {
    await QuotaUtils.requestFinished(Services.qms.init());
    await QuotaUtils.requestFinished(Services.qms.initTemporaryStorage());
    await QuotaUtils.requestFinished(
      Services.qms.initializeTemporaryOrigin("default", principal)
    );
  }
  failed.file.remove(false);
  await IOUtils.writeUTF8(failed.file.path, "orphan");
  await QuotaUtils.requestFinished(Services.qms.init());
  await QuotaUtils.requestFinished(Services.qms.initTemporaryStorage());
  await QuotaUtils.requestFinished(
    Services.qms.initializeTemporaryOrigin("default", principal)
  );
  if (cleanupOnOpen) {
    Assert.ok(failed.file.exists(), "The blob remains until database open");
    const usageBefore = await getCachedUsageForOrigin(principal);
    await openOPFS(principal);
    Assert.equal(
      usageBefore - (await getCachedUsageForOrigin(principal)),
      6,
      "Database open accounts for the successful retry"
    );
  }
  Assert.ok(!failed.file.exists(), "The failed removal is retried");
  {
    const db = Services.storage.openUnsharedDatabase(dbFile);
    Assert.equal(
      queryInt(
        db,
        `SELECT count(*) FROM FileIds WHERE fileId = X'${failed.hex}'`
      ),
      0
    );
    Assert.equal(
      queryInt(
        db,
        `SELECT count(*) FROM Usages WHERE handle = X'${failed.hex}'`
      ),
      0,
      "The successful retry cascades to Usages"
    );
    db.close();
  }
  Assert.equal(
    await evalInOPFSSandbox(
      principal,
      `(async () => {
        const root = await storage.getDirectory();
        const handle = await root.getFileHandle("kept.txt");
        return (await handle.getFile()).text();
      })().then(resolve, reject);`
    ),
    "keep this data",
    "Startup cleanup preserves the main file's contents"
  );
}

add_task(async function test_startup_removes_blobs_and_retries_failures() {
  await testOrphanCleanup(false);
});

add_task(
  async function test_database_open_removes_blobs_and_retries_failures() {
    try {
      await testOrphanCleanup(true);
    } finally {
      Services.prefs.setBoolPref("dom.quotaManager.loadQuotaFromCache", false);
      Services.prefs.setBoolPref(
        "dom.quotaManager.loadQuotaFromSecondaryCache",
        false
      );
    }
  }
);
