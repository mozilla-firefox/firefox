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
