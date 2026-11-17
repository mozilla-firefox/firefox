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

const TASK_NAME = "test_model_hub_delete";

const CONFIG_CONTENT = JSON.stringify({ hidden_size: 32 });

/**
 * @param {ArrayBuffer} buffer
 * @returns {string}
 */
function decode(buffer) {
  return new TextDecoder().decode(new Uint8Array(buffer));
}

/**
 * Stand up a mock hub plus a ModelHub pointed at it, with the download and
 * inspection helpers these tests share. Every file defaults to MODEL at
 * REVISION, so a test only names what it varies.
 *
 * The caller is responsible for calling the returned cleanup.
 */
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

    // The cache keys models by hub hostname, and the delete APIs take that
    // qualified form rather than the name the download was requested under.
    qualified: `${hostname}/${MODEL}`,

    serve({ revision = REVISION, file, content = CONFIG_CONTENT }) {
      return hub.setFile({ model: MODEL, revision, file, content });
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

    gets({ revision = REVISION, file }) {
      return hub.requestCount({ model: MODEL, revision, file, method: "GET" });
    },

    /** @returns {Promise<boolean>} */
    async opfsHasFile({ revision = REVISION, file }) {
      const path = modelHub.cache.generateFilePathInOPFS({
        model: `${hostname}/${MODEL}`,
        revision,
        file,
      });
      try {
        await SharedOPFS.getFileHandle(path);
        return true;
      } catch (error) {
        if (error.name === "NotFoundError") {
          return false;
        }
        throw error;
      }
    },

    /** @returns {Promise<boolean>} */
    async opfsHasDirectory({ revision = REVISION }) {
      const path = modelHub.cache.generateFilePathInOPFS({
        model: `${hostname}/${MODEL}`,
        revision,
        file: "unused",
      });
      try {
        await SharedOPFS.getDirectoryHandle(
          path.slice(0, path.lastIndexOf("/"))
        );
        return true;
      } catch (error) {
        if (error.name === "NotFoundError") {
          return false;
        }
        throw error;
      }
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
 * Ensure deleting a model removes files that were really downloaded, rather
 * than only the rows describing them, and that the model can be fetched again
 * afterwards.
 */
add_task(async function test_delete_model_removes_downloaded_files() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG });
  hub.serve({ file: WEIGHTS });
  await hub.download({ file: CONFIG });
  await hub.download({ file: WEIGHTS });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [{ name: hub.qualified, revision: REVISION, tasks: [TASK_NAME] }],
    "The downloaded model is listed."
  );
  Assert.ok(await hub.opfsHasFile({ file: CONFIG }), "config.json is on disk.");
  Assert.ok(
    await hub.opfsHasFile({ file: WEIGHTS }),
    "The weights are on disk."
  );

  await hub.modelHub.deleteModels({
    model: hub.qualified,
    revision: REVISION,
    taskName: null,
  });

  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [],
    "No models remain listed."
  );
  Assert.ok(
    !(await hub.opfsHasFile({ file: CONFIG })),
    "config.json was removed from disk, not just from the index."
  );
  Assert.ok(
    !(await hub.opfsHasFile({ file: WEIGHTS })),
    "The weights were removed from disk."
  );

  const [buffer] = await hub.download({ file: CONFIG });

  Assert.equal(
    decode(buffer),
    CONFIG_CONTENT,
    "The model can be downloaded again after deletion."
  );
  Assert.equal(
    hub.gets({ file: CONFIG }),
    2,
    "The re-read went back to the network, so the delete really emptied the cache."
  );

  await hub.cleanup();
});

/**
 * Ensure a file two tasks share outlives the deletion of one of them. The
 * second task's download is a cache hit, so without this refcount it would be
 * deleting a file out from under a task that still needs it.
 */
add_task(async function test_delete_task_keeps_file_used_by_another_task() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG });

  await hub.download({ file: CONFIG, taskName: "task-a" });
  await hub.download({ file: CONFIG, taskName: "task-b" });

  Assert.equal(
    hub.gets({ file: CONFIG }),
    1,
    "The second task reused the file the first downloaded."
  );

  await hub.modelHub.deleteModels({
    taskName: "task-a",
    model: null,
    revision: null,
  });

  Assert.ok(
    await hub.opfsHasFile({ file: CONFIG }),
    "The file survives, because task-b still references it."
  );

  await hub.modelHub.deleteModels({
    taskName: "task-b",
    model: null,
    revision: null,
  });

  Assert.ok(
    !(await hub.opfsHasFile({ file: CONFIG })),
    "Removing the last task that references the file deletes it."
  );

  await hub.cleanup();
});

/**
 * Ensure the same refcount holds for engines, which is the uninstall path: a
 * feature going away must not take a file another feature is still using.
 */
add_task(async function test_delete_engine_keeps_file_used_by_another_engine() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG });

  await hub.download({ file: CONFIG, engineId: "engine-one" });
  await hub.download({ file: CONFIG, engineId: "engine-two" });

  await hub.modelHub.deleteFilesByEngine({ engineId: "engine-one" });

  Assert.ok(
    await hub.opfsHasFile({ file: CONFIG }),
    "The file survives, because engine-two still references it."
  );

  await hub.modelHub.deleteFilesByEngine({ engineId: "engine-two" });

  Assert.ok(
    !(await hub.opfsHasFile({ file: CONFIG })),
    "Removing the last engine that references the file deletes it."
  );
  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [],
    "The model is no longer listed."
  );

  await hub.cleanup();
});

/**
 * Ensure upgrading to a new revision collects the old one. Both revisions are
 * downloaded in full first, which is what happens in practice: the new
 * revision is fetched before anything knows the old one is obsolete.
 */
add_task(async function test_delete_non_matching_revisions() {
  const hub = await withModelHub();
  hub.serve({ file: CONFIG, revision: "v1" });
  hub.serve({ file: CONFIG, revision: "v2" });

  await hub.download({ file: CONFIG, revision: "v1" });
  await hub.download({ file: CONFIG, revision: "v2" });

  await hub.modelHub.deleteNonMatchingModelRevisions({
    taskName: TASK_NAME,
    modelWithHostname: hub.qualified,
    targetRevision: "v2",
  });

  Assert.ok(
    !(await hub.opfsHasFile({ file: CONFIG, revision: "v1" })),
    "The superseded revision was removed from disk."
  );
  Assert.ok(
    await hub.opfsHasFile({ file: CONFIG, revision: "v2" }),
    "The revision being kept is untouched."
  );
  Assert.deepEqual(
    await hub.modelHub.listModels(),
    [{ name: hub.qualified, revision: "v2", tasks: [TASK_NAME] }],
    "Only the kept revision is listed."
  );

  await hub.cleanup();
});

/**
 * Deleting a model removes the directories that held its files, which would
 * otherwise accumulate for every model and revision ever downloaded. WEIGHTS
 * sits in a subdirectory, so this covers pruning more than one level.
 */
add_task(async function test_delete_prunes_empty_directories() {
  const hub = await withModelHub();
  hub.serve({ file: WEIGHTS });
  await hub.download({ file: WEIGHTS });

  await hub.modelHub.deleteModels({
    model: hub.qualified,
    revision: REVISION,
    taskName: null,
  });

  Assert.ok(
    !(await hub.opfsHasFile({ file: WEIGHTS })),
    "The file itself is gone."
  );
  Assert.ok(
    !(await hub.opfsHasDirectory({})),
    "The directory that held it is gone too."
  );

  await hub.cleanup();
});

/**
 * Pruning stops at the first directory that still holds something, so deleting
 * one revision does not take the directories a sibling revision needs.
 */
add_task(async function test_delete_keeps_directories_a_sibling_needs() {
  const hub = await withModelHub();
  hub.serve({ file: WEIGHTS, revision: "v1" });
  hub.serve({ file: WEIGHTS, revision: "v2" });
  await hub.download({ file: WEIGHTS, revision: "v1" });
  await hub.download({ file: WEIGHTS, revision: "v2" });

  await hub.modelHub.deleteModels({
    model: hub.qualified,
    revision: "v1",
    taskName: null,
  });

  Assert.ok(
    !(await hub.opfsHasDirectory({ revision: "v1" })),
    "The deleted revision's directory is pruned."
  );
  Assert.ok(
    await hub.opfsHasFile({ file: WEIGHTS, revision: "v2" }),
    "The sibling revision's file is untouched."
  );
  Assert.ok(
    await hub.opfsHasDirectory({ revision: "v2" }),
    "The sibling revision's directory survives, so pruning stopped in time."
  );

  await hub.cleanup();
});
