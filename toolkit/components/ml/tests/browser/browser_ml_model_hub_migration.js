/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const MODEL = "acme/bert";
const REVISION = "main";
const STALE_REVISION = "old-empty-revision";
const ORPHAN_TASK_REVISION = "orphan-task-revision";
const TASK_NAME = "test_model_hub_migration";

const HEALTHY_FILE = "config.json";
const HEALTHY_CONTENT = JSON.stringify({ hidden_size: 32 });
const MISSING_FILE_FILE = "missing-on-disk.bin";
const ORPHAN_FILE_FILE = "orphan-file.bin";
const ORPHAN_TASK_FILE = "orphan-task.bin";
const ZERO_LENGTH_FILE = "zero-length.bin";

/**
 * @param {ArrayBuffer} buffer
 * @returns {string}
 */
function decode(buffer) {
  return new TextDecoder().decode(new Uint8Array(buffer));
}

/**
 * @param {string} qualifiedModel - The hostname-qualified model name.
 * @returns {Promise<void>}
 */
async function seedVersion8Database(qualifiedModel) {
  const cache = await IndexedDBCache.init({ version: 8, reset: true });

  await cache.put({
    taskName: TASK_NAME,
    model: qualifiedModel,
    revision: REVISION,
    file: HEALTHY_FILE,
    data: new Blob([HEALTHY_CONTENT]),
  });

  await cache.put({
    taskName: TASK_NAME,
    model: qualifiedModel,
    revision: REVISION,
    file: MISSING_FILE_FILE,
    data: new Blob(["this file is about to disappear from disk"]),
  });
  await SharedOPFS.remove(
    cache.generateFilePathInOPFS({
      model: qualifiedModel,
      revision: REVISION,
      file: MISSING_FILE_FILE,
    })
  );

  await SharedOPFS.getFileHandle(
    cache.generateFilePathInOPFS({
      model: qualifiedModel,
      revision: REVISION,
      file: ORPHAN_FILE_FILE,
    }),
    { create: true }
  );

  await cache.put({
    taskName: TASK_NAME,
    model: qualifiedModel,
    revision: REVISION,
    file: ZERO_LENGTH_FILE,
    data: new Blob(["this file will be truncated to zero bytes"]),
  });
  const zeroLengthHandle = await SharedOPFS.getFileHandle(
    cache.generateFilePathInOPFS({
      model: qualifiedModel,
      revision: REVISION,
      file: ZERO_LENGTH_FILE,
    })
  );
  await (
    await zeroLengthHandle.createWritable({ keepExistingData: false })
  ).close();

  await new Promise((resolve, reject) => {
    const transaction = cache.db.transaction(
      [cache.taskStoreName],
      "readwrite"
    );
    transaction.objectStore(cache.taskStoreName).put({
      taskName: TASK_NAME,
      model: qualifiedModel,
      revision: ORPHAN_TASK_REVISION,
      file: ORPHAN_TASK_FILE,
    });
    transaction.oncomplete = resolve;
    transaction.onerror = () => reject(transaction.error);
  });

  const staleDirPath = cache
    .generateFilePathInOPFS({
      model: qualifiedModel,
      revision: STALE_REVISION,
      file: "unused",
    })
    .slice(0, -"/unused".length);
  await SharedOPFS.getDirectoryHandle(staleDirPath, { create: true });

  await cache.dispose();
}

/**
 * Add stale file entries that represent correctness bugs that need upgrading.
 */
add_task(async function test_migration_repairs_stale_entries() {
  const hub = MLTestUtils.createMockModelHub();
  const hostname = new URL(hub.rootUrl).hostname;
  const qualifiedModel = `${hostname}/${MODEL}`;

  await seedVersion8Database(qualifiedModel);

  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
  });

  const { files } = await modelHub.listFiles({
    model: qualifiedModel,
    revision: REVISION,
  });

  Assert.deepEqual(
    files.map(file => file.path).sort(),
    [HEALTHY_FILE],
    "Only the entry with a real file on disk survives the repair."
  );

  Assert.deepEqual(
    await modelHub.listModels(),
    [{ name: qualifiedModel, revision: REVISION, tasks: [TASK_NAME] }],
    "The orphan task row is gone, so it does not conjure up a phantom model."
  );

  const [healthyBlob] = await modelHub.cache.getFile({
    model: qualifiedModel,
    revision: REVISION,
    file: HEALTHY_FILE,
  });
  Assert.equal(
    await healthyBlob.text(),
    HEALTHY_CONTENT,
    "The healthy file's bytes are untouched by the repair."
  );

  for (const file of [
    MISSING_FILE_FILE,
    ORPHAN_FILE_FILE,
    ORPHAN_TASK_FILE,
    ZERO_LENGTH_FILE,
  ]) {
    let foundOnDisk = true;
    try {
      await SharedOPFS.getFileHandle(
        modelHub.cache.generateFilePathInOPFS({
          model: qualifiedModel,
          revision: REVISION,
          file,
        })
      );
    } catch (error) {
      if (error.name !== "NotFoundError") {
        throw error;
      }
      foundOnDisk = false;
    }
    Assert.ok(!foundOnDisk, `${file} was removed from OPFS.`);
  }

  const staleDirPath = modelHub.cache
    .generateFilePathInOPFS({
      model: qualifiedModel,
      revision: STALE_REVISION,
      file: "unused",
    })
    .slice(0, -"/unused".length);
  let staleDirectoryExists = true;
  try {
    await SharedOPFS.getDirectoryHandle(staleDirPath);
  } catch (error) {
    if (error.name !== "NotFoundError") {
      throw error;
    }
    staleDirectoryExists = false;
  }
  Assert.ok(
    !staleDirectoryExists,
    "The empty leftover revision directory was pruned."
  );

  await deleteCache(modelHub.cache);
  await hub.cleanup();
});

/**
 * Ensures that opening the cache during repair does not cause newly cached
 * files to be deleted.
 */
add_task(async function test_migration_preserves_concurrent_cache_writes() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  const repairStarted = Promise.withResolvers();
  const releaseRepair = Promise.withResolvers();
  const originalGetFileHandle = SharedOPFS.getFileHandle;
  const openedCaches = [];
  let repair;
  let concurrentWrite;

  try {
    const oldCache = await IndexedDBCache.init({ dbName, version: 8 });
    openedCaches.push(oldCache);
    await oldCache.put({
      taskName: TASK_NAME,
      model: MODEL,
      revision: REVISION,
      file: HEALTHY_FILE,
      data: new Blob([HEALTHY_CONTENT]),
    });
    const healthyPath = oldCache.generateFilePathInOPFS({
      model: MODEL,
      revision: REVISION,
      file: HEALTHY_FILE,
    });
    await oldCache.dispose();

    SharedOPFS.getFileHandle = async (...args) => {
      if (args[0] === healthyPath) {
        repairStarted.resolve();
        await releaseRepair.promise;
      }

      return originalGetFileHandle(...args);
    };

    info("Pause repair after it has read the existing headers.");
    repair = IndexedDBCache.init({ dbName }).then(cache => {
      openedCaches.push(cache);

      return cache;
    });
    await repairStarted.promise;

    info("An unrelated database can initialize while repair is paused.");
    const independentCache = await IndexedDBCache.init({
      dbName: `${dbName}-independent`,
    });
    openedCaches.push(independentCache);
    Assert.ok(
      independentCache.isOpen(),
      "Other databases do not wait for repair."
    );

    info("Request a second connection while repair is paused.");
    let secondInitialized = false;
    const entry = {
      model: MODEL,
      revision: REVISION,
      file: "concurrent.bin",
    };
    concurrentWrite = IndexedDBCache.init({ dbName }).then(async cache => {
      openedCaches.push(cache);
      secondInitialized = true;
      await cache.put({
        ...entry,
        taskName: "concurrent-task",
        engineId: "concurrent-engine",
        data: new Blob(["written by the concurrent caller"]),
      });

      return cache;
    });
    await Promise.resolve();

    // Opens for the same database are processed in request order. This open
    // completes after any open already issued by the second initialization.
    await new Promise((resolve, reject) => {
      const request = indexedDB.openForPrincipal(
        Services.scriptSecurityManager.getSystemPrincipal(),
        dbName,
        9
      );
      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        request.result.close();
        resolve();
      };
    });
    Assert.ok(
      !secondInitialized,
      "The second initialization waits for repair before exposing the cache."
    );

    info("Finish repair and check that the concurrent entry survives.");
    releaseRepair.resolve();
    await repair;
    const otherCache = await concurrentWrite;

    Assert.ok(
      await otherCache.fileExists(entry),
      "Repair preserves the file written through the second connection."
    );
    Assert.ok(
      await otherCache._testGetData(otherCache.taskStoreName, [
        "concurrent-task",
        MODEL,
        REVISION,
        entry.file,
      ]),
      "Repair preserves the concurrent entry's task ownership."
    );
  } finally {
    releaseRepair.resolve();
    await Promise.allSettled([repair, concurrentWrite]);
    SharedOPFS.getFileHandle = originalGetFileHandle;
    for (const cache of openedCaches) {
      await cache.dispose();
    }
    for (const name of [dbName, `${dbName}-independent`]) {
      await IndexedDBCache.deleteDatabaseAndWait(
        Services.scriptSecurityManager.getSystemPrincipal(),
        name
      );
    }
  }
});

/**
 * Confirm the migration redownloads a file that was previously stuck.
 */
add_task(async function test_migration_allows_redownload_on_demand() {
  const hub = MLTestUtils.createMockModelHub();
  const hostname = new URL(hub.rootUrl).hostname;
  const qualifiedModel = `${hostname}/${MODEL}`;

  await seedVersion8Database(qualifiedModel);

  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
  });

  // Trigger the migration before re-serving the file, so the assertion below
  // is really exercising a redownload rather than an initial one.
  await modelHub.listModels();

  hub.setFile({
    model: MODEL,
    revision: REVISION,
    file: MISSING_FILE_FILE,
    content: "redownloaded",
  });

  const [buffer] = await modelHub.getModelFileAsArrayBuffer({
    model: MODEL,
    revision: REVISION,
    file: MISSING_FILE_FILE,
    taskName: TASK_NAME,
  });

  Assert.equal(
    decode(buffer),
    "redownloaded",
    "The file the repair dropped is fetched fresh rather than staying broken."
  );
  Assert.equal(
    hub.requestCount({
      model: MODEL,
      revision: REVISION,
      file: MISSING_FILE_FILE,
      method: "GET",
    }),
    1,
    "The redownload really went to the network, proving the stale entry was gone."
  );

  await deleteCache(modelHub.cache);
  await hub.cleanup();
});

const LEGACY_ENGINE_ID = "wllamapreview";
const RENAMED_ENGINE_ID = "link-preview";
const LEGACY_TASK_NAME = "wllama-text-generation";
const RENAMED_TASK_NAME = "llama-text-generation";

/**
 * Bug 1967279: Verify the 6 -> 7 schema migration rewrites a row tagged with
 * the old "wllamapreview" engineId to "link-preview" without dropping the
 * underlying OPFS model file, so the renamed feature can still uninstall it.
 */
add_task(async function test_migration_renames_legacy_engine_id() {
  const model = "org/link-preview";
  const revision = "v1";
  const file = "weights.bin";

  // Seed a row under the pre-migration schema with the obsolete engineId.
  let cache = await IndexedDBCache.init({ version: 6, reset: true });
  await cache.put({
    engineId: LEGACY_ENGINE_ID,
    taskName: LEGACY_TASK_NAME,
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  await cache.dispose();

  // Reopen at the current schema version and confirm the rename happened.
  cache = await IndexedDBCache.init({});

  let listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.metadata.engineIds,
    [RENAMED_ENGINE_ID],
    "Migration should rewrite the legacy engineId to its replacement."
  );
  Assert.equal(
    listed.files.length,
    1,
    "Migration must not drop the OPFS model file."
  );

  const opfsPath = cache.generateFilePathInOPFS({ model, revision, file });
  const fileHandle = await SharedOPFS.getFileHandle(opfsPath);
  Assert.notEqual(
    fileHandle,
    null,
    "The model file exists in OPFS before uninstall."
  );

  // The uninstall path used by LinkPreview should now find and remove the row.
  await cache.deleteFilesByEngine({ engineId: RENAMED_ENGINE_ID });
  listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.files,
    [],
    "Uninstall via the new engineId removes the file from the cache listing."
  );

  await Assert.rejects(
    SharedOPFS.getFileHandle(opfsPath),
    error => error.name === "NotFoundError",
    "Uninstall physically deletes the cached model file."
  );

  await deleteCache(cache);
});

/**
 * Bug 1967279: If a row already carries both the legacy and the new engineId
 * (e.g. a re-download happened before the migration), the rewrite must
 * de-duplicate rather than leaving the stale id behind.
 */
add_task(async function test_migration_dedups_legacy_and_new_engine_ids() {
  const model = "org/link-preview";
  const revision = "v1";
  const file = "weights.bin";

  let cache = await IndexedDBCache.init({ version: 6, reset: true });
  await cache.put({
    engineId: LEGACY_ENGINE_ID,
    taskName: LEGACY_TASK_NAME,
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  await cache.put({
    engineId: RENAMED_ENGINE_ID,
    taskName: LEGACY_TASK_NAME,
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  await cache.dispose();

  cache = await IndexedDBCache.init({});
  const listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.metadata.engineIds,
    [RENAMED_ENGINE_ID],
    "Migration should collapse duplicate ids to the new engineId only."
  );

  await deleteCache(cache);
});

/**
 * Verify the 7 -> 8 schema migration re-keys task rows whose taskName was
 * renamed, so already-downloaded model files keep hitting the cache instead
 * of being downloaded again.
 */
add_task(async function test_migration_renames_legacy_task_name() {
  const model = "org/link-preview";
  const revision = "v1";
  const file = "weights.bin";

  // Seed a row under the pre-migration schema with the obsolete taskName.
  let cache = await IndexedDBCache.init({ version: 7, reset: true });
  await cache.put({
    engineId: RENAMED_ENGINE_ID,
    taskName: LEGACY_TASK_NAME,
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  await cache.dispose();

  // Reopen at the current schema version and confirm the row was re-keyed.
  cache = await IndexedDBCache.init({});

  let listed = await cache.listFiles({
    taskName: RENAMED_TASK_NAME,
    model,
    revision,
  });
  Assert.equal(
    listed.files.length,
    1,
    "Migration should re-key the row to the new taskName."
  );

  listed = await cache.listFiles({
    taskName: LEGACY_TASK_NAME,
    model,
    revision,
  });
  Assert.deepEqual(
    listed.files,
    [],
    "No row should remain under the old taskName."
  );

  await deleteCache(cache);
});
