/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const MODEL = "acme/bert";
const REVISION = "v1";
const CONFIG = "config.json";
const WEIGHTS = "onnx/model.onnx";
const TASK_NAME = "test_model_hub_list";

// Distinct lengths, so an aggregated size can only be right by adding both.
const CONFIG_BYTES = 10;
const WEIGHTS_BYTES = 15;

async function withModelHub() {
  const hub = MLTestUtils.createMockModelHub();
  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
    reset: true,
  });
  const hostname = new URL(hub.rootUrl).hostname;

  return {
    hub,
    modelHub,
    qualified: `${hostname}/${MODEL}`,

    serve({ revision = REVISION, file, size }) {
      return hub.setFile({
        model: MODEL,
        revision,
        file,
        content: "x".repeat(size),
      });
    },

    download({ revision = REVISION, file, taskName = TASK_NAME, engineId }) {
      return modelHub.getModelFileAsArrayBuffer({
        model: MODEL,
        revision,
        file,
        taskName,
        engineId,
      });
    },

    async cleanup() {
      if (modelHub.cache) {
        await deleteCache(modelHub.cache);
      }
      await hub.cleanup();
    },
  };
}

/**
 * Ensure a downloaded model shows up in the listing, keyed by the hostname
 * the cache qualifies it with, and that revisions are listed separately.
 */
add_task(async function test_list_models_reports_downloads() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });
  hub.serve({ file: CONFIG, revision: "v2", size: CONFIG_BYTES });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [],
    "Nothing is listed before anything is downloaded."
  );

  await hub.download({ file: CONFIG });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [{ taskName: TASK_NAME, name: hub.qualified, revision: REVISION }],
    "The downloaded model is listed under the hostname-qualified name."
  );

  await hub.download({ file: CONFIG, revision: "v2" });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [
      { taskName: TASK_NAME, name: hub.qualified, revision: REVISION },
      { taskName: TASK_NAME, name: hub.qualified, revision: "v2" },
    ],
    "Each revision is listed separately."
  );

  await hub.cleanup();
});

/**
 * listModels reports one row per model and revision, not per task.
 *
 * TODO: it walks the model/revision index with a "nextunique" cursor, which
 * keeps only the first record per index key in primary-key order. That key
 * starts with the task name, so when several tasks share a model the surviving
 * row names whichever task sorts first - download order has nothing to do with
 * it. A caller asking which tasks hold a model cannot tell from this.
 */
add_task(async function test_list_models_collapses_tasks() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });

  // Downloaded in reverse alphabetical order, so the surviving row cannot be
  // explained by which task got there first.
  await hub.download({ file: CONFIG, taskName: "task-zulu" });
  await hub.download({ file: CONFIG, taskName: "task-alpha" });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [{ taskName: "task-alpha", name: hub.qualified, revision: REVISION }],
    "Two tasks share one row, named for the task that sorts first."
  );

  await hub.cleanup();
});

/**
 * Ensure listing a model's files reports what was actually stored, including
 * sizes totalled across the files.
 */
add_task(async function test_list_files_reports_stored_files() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });
  hub.serve({ file: WEIGHTS, size: WEIGHTS_BYTES });
  await hub.download({ file: CONFIG });
  await hub.download({ file: WEIGHTS });

  const { files, metadata } = await hub.modelHub.listFiles({
    model: hub.qualified,
    revision: REVISION,
  });

  Assert.deepEqual(
    files.map(entry => entry.path),
    [CONFIG, WEIGHTS],
    "Both files are listed, subdirectory and all."
  );
  Assert.deepEqual(
    files.map(entry => entry.headers.fileSize),
    [CONFIG_BYTES, WEIGHTS_BYTES],
    "Each file reports the size that was downloaded."
  );
  Assert.equal(
    metadata.totalSize,
    CONFIG_BYTES + WEIGHTS_BYTES,
    "The total is the sum of the files."
  );
  Assert.greater(metadata.lastUsed, 0, "A last-used time was recorded.");

  await hub.cleanup();
});

/**
 * Ensure listFiles refuses a query it cannot answer rather than returning an
 * empty list that reads as "nothing cached".
 */
add_task(async function test_list_files_requires_a_model_or_a_task() {
  const hub = await withModelHub();

  await Assert.rejects(
    hub.modelHub.listFiles({}),
    /Both model and revision must be defined/,
    "Naming neither a task nor a model and revision is an error."
  );

  await hub.cleanup();
});

/**
 * listFiles cannot be called by task name alone..
 *
 * TODO: the loop that looks up each file's engines keys off the `model` and
 * `revision` arguments rather than the pair it just resolved from the task, so
 * they are undefined on this path and IndexedDB rejects the key. Only callers
 * that pass model and revision alongside taskName work, which is why the
 * existing IndexedDBCache tests never hit this.
 */
add_task(async function test_list_files_by_task_name_alone_throws() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });
  await hub.download({ file: CONFIG });

  await Assert.rejects(
    hub.modelHub.listFiles({ taskName: TASK_NAME }),
    /Data provided to an operation does not meet requirements/,
    "Listing a task's files without also naming the model throws a DataError."
  );

  await hub.cleanup();
});

/**
 * The engine attribution in a file listing is wrong in two ways.
 *
 * TODO: every file reports an empty `engineIds`, because the array pushed into
 * the result is hardcoded rather than the one looked up. The aggregated
 * `metadata.engineIds` is assigned per file instead of unioned, so it ends up
 * holding only the last file's engines - here that hides engine-two entirely,
 * even though the delete path tracks it correctly.
 */
add_task(async function test_list_files_misreports_engines() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });
  hub.serve({ file: WEIGHTS, size: WEIGHTS_BYTES });

  await hub.download({ file: CONFIG, engineId: "engine-one" });
  await hub.download({ file: WEIGHTS, engineId: "engine-one" });
  await hub.download({ file: CONFIG, engineId: "engine-two" });

  const { files, metadata } = await hub.modelHub.listFiles({
    model: hub.qualified,
    revision: REVISION,
  });

  Assert.deepEqual(
    files.map(entry => entry.engineIds),
    [[], []],
    "Per-file engine ids are always empty."
  );
  Assert.deepEqual(
    metadata.engineIds,
    ["engine-one"],
    "The aggregate reports only the last file's engines, losing engine-two."
  );

  await hub.cleanup();
});
