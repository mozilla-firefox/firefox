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
    [{ name: hub.qualified, revision: REVISION, tasks: [TASK_NAME] }],
    "The downloaded model is listed under the hostname-qualified name."
  );

  await hub.download({ file: CONFIG, revision: "v2" });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [
      { name: hub.qualified, revision: REVISION, tasks: [TASK_NAME] },
      { name: hub.qualified, revision: "v2", tasks: [TASK_NAME] },
    ],
    "Each revision is listed separately."
  );

  await hub.cleanup();
});

/**
 * Several tasks can share one model. listModels names every task that
 * holds it, so callers can tell when a model is still in use elsewhere.
 */
add_task(async function test_list_models_names_every_task() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });

  // Downloaded in reverse alphabetical order, so the reported set cannot be
  // explained by which task got there first.
  await hub.download({ file: CONFIG, taskName: "task-zulu" });
  await hub.download({ file: CONFIG, taskName: "task-alpha" });

  const models = await hub.modelHub.listModels();

  Assert.deepEqual(
    models.map(({ name, revision }) => ({ name, revision })),
    [{ name: hub.qualified, revision: REVISION }],
    "Two tasks sharing a model still make one row."
  );
  Assert.deepEqual(
    [...models[0].tasks].sort(),
    ["task-alpha", "task-zulu"],
    "Both tasks holding the model are named."
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
 * listFiles can be called by task name alone.
 */
add_task(async function test_list_files_by_task_name_alone() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, size: CONFIG_BYTES });
  await hub.download({ file: CONFIG, engineId: "engine-one" });

  const { files, metadata } = await hub.modelHub.listFiles({
    taskName: TASK_NAME,
  });

  Assert.deepEqual(
    files.map(entry => entry.path),
    [CONFIG],
    "The task's files are listed without also naming the model."
  );
  Assert.deepEqual(
    metadata.engineIds,
    ["engine-one"],
    "The engines resolved through the task are reported."
  );
  Assert.equal(metadata.totalSize, CONFIG_BYTES, "The size is aggregated.");

  await hub.cleanup();
});

/**
 * A file listing attributes engines per file, and aggregates them across every
 * file rather than reporting whichever file happened to be looked at last.
 */
add_task(async function test_list_files_reports_engines() {
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

  const enginesByPath = Object.fromEntries(
    files.map(entry => [entry.path, [...entry.engineIds].sort()])
  );

  Assert.deepEqual(
    enginesByPath,
    {
      [CONFIG]: ["engine-one", "engine-two"],
      [WEIGHTS]: ["engine-one"],
    },
    "Each file reports the engines that asked for it."
  );
  Assert.deepEqual(
    [...metadata.engineIds].sort(),
    ["engine-one", "engine-two"],
    "The aggregate is the union across files, so engine-two is not lost."
  );

  await hub.cleanup();
});
