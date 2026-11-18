/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

// A pinned revision, so the cached-file decision comes from
// IndexedDBCache.fileExists rather than from an ETag comparison against the
// hub. PINNED_FILE and MAIN_FILE differ only in revision, and take different
// paths through getModelDataAsFile because of it.
const PINNED_FILE = {
  model: "acme/bert",
  revision: "v1",
  file: "onnx/model.onnx",
};

const MAIN_FILE = { ...PINNED_FILE, revision: "main" };

const TASK_NAME = "test_model_hub_download_failures";

// Large enough that the interruption point lands well short of the end.
const BODY_SIZE = 1024 * 1024;

// A multiple of the mock hub's 16 KiB streaming chunk, as setFile requires.
const FAIL_AFTER_BYTES = 256 * 1024;

// Where test_aborted_download_rejects pauses, short of BODY_SIZE.
const PAUSE_AFTER_BYTES = 256 * 1024;

/**
 * @param {number} size
 * @returns {Uint8Array}
 */
function makeBodyContent(size) {
  const bytes = new Uint8Array(size);
  for (let i = 0; i < size; i++) {
    bytes[i] = i % 251;
  }
  return bytes;
}

/**
 * @param {object} modelHub
 * @param {object} triple
 * @param {string} triple.model
 * @param {string} triple.revision
 * @param {string} triple.file
 * @returns {Promise<{path: string, exists: boolean, size: number|null}>}
 */
async function opfsEntry(modelHub, { model, revision, file }) {
  const hostname = new URL(modelHub.rootUrl).hostname;
  const path = modelHub.cache.generateFilePathInOPFS({
    model: `${hostname}/${model}`,
    revision,
    file,
  });

  try {
    const handle = await SharedOPFS.getFileHandle(path);
    return { path, exists: true, size: (await handle.getFile()).size };
  } catch (error) {
    if (error.name === "NotFoundError") {
      return { path, exists: false, size: null };
    }
    throw error;
  }
}

/**
 * Checks whether the cache has the headers or not.
 *
 * @param {object} modelHub
 * @param {object} triple
 * @param {string} triple.model
 * @param {string} triple.revision
 * @param {string} triple.file
 * @returns {Promise<boolean>}
 */
async function hasHeaders(modelHub, { model, revision, file }) {
  const hostname = new URL(modelHub.rootUrl).hostname;
  const headers = await modelHub.cache.getHeaders({
    model: `${hostname}/${model}`,
    revision,
    file,
  });
  return headers !== undefined;
}

/**
 * Stand up a mock hub serving one large model file, plus a ModelHub pointed at
 * it. The caller is responsible for calling the returned cleanup.
 *
 * @param {object} options
 * @param {object} options.modelFile - The {model, revision, file} to serve.
 * @param {?number} [options.failAfterBytes] - Drop the connection after this
 *   many bytes of an advertised full-length body.
 * @param {?number} [options.pauseAfterBytes] - Send only this many bytes of
 *   an advertised full-length body, then hold the connection open until
 *   `hub.resumeAfterPause()` is called.
 * @param {number} [options.failTimes] - Answer this many requests with
 *   `failStatus` before serving anything.
 * @param {number} [options.failStatus] - The status to fail with.
 */
async function withModelHub({
  modelFile,
  failAfterBytes = null,
  pauseAfterBytes = null,
  failTimes = 0,
  failStatus = 500,
}) {
  const hub = MLTestUtils.createMockModelHub();
  const served = hub.setFile({
    ...modelFile,
    content: makeBodyContent(BODY_SIZE),
    failAfterBytes,
    pauseAfterBytes,
    failTimes,
    failStatus,
  });

  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
    reset: true,
  });

  return {
    hub,
    modelHub,
    served,
    args: { ...modelFile, taskName: TASK_NAME },

    serveInFull() {
      hub.setFile({
        ...modelFile,
        content: makeBodyContent(BODY_SIZE),
        etag: served.etag,
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
 * Ensure a failed transfer leaves a clean state.
 *
 * TODO: Currently OPFS does not correctly clean up after itself and leaves 0 byte
 * entries behind.
 */
add_task(async function test_interrupted_transfer_rejects() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: PINNED_FILE,
    failAfterBytes: FAIL_AFTER_BYTES,
  });

  await Assert.rejects(
    modelHub.getModelFileAsBlob(args),
    /Failed to fetch the model file/,
    "A body that stops short of its Content-Length rejects."
  );

  Assert.equal(
    hub.requestCount({ ...PINNED_FILE, method: "GET" }),
    1,
    "The body was requested once, and the failure is not retried."
  );

  const entry = await opfsEntry(modelHub, PINNED_FILE);
  info(`OPFS entry after an interrupted transfer: ${JSON.stringify(entry)}`);

  // TODO - Clean up.
  Assert.ok(
    entry.exists,
    "The OPFS entry outlives the failed download. OPFS.download creates it " +
      "with getFileHandle({create: true}) before any bytes are written, and " +
      "the error path in getModelDataAsFile does not remove it."
  );
  Assert.equal(
    entry.size,
    0,
    "The entry is empty rather than holding the bytes that did arrive. Those " +
      "went to the temporary file behind createWritable, which the OPFS swap " +
      "discards when the stream aborts."
  );
  Assert.ok(
    !(await hasHeaders(modelHub, PINNED_FILE)),
    "No headers row was written, so IndexedDB does not consider it cached."
  );

  await cleanup();
});

/**
 * Test what happens when a download is intentionally cancelled.
 *
 * TODO: Currently OPFS does not correctly clean up after itself and leaves 0 byte
 * entries behind.
 */
add_task(async function test_aborted_download_rejects() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: PINNED_FILE,
    pauseAfterBytes: PAUSE_AFTER_BYTES,
  });

  const abortController = new AbortController();
  let progressAtAbort = null;

  await Assert.rejects(
    modelHub.getModelFileAsBlob({
      ...args,
      abortSignal: abortController.signal,
      progressCallback: progressData => {
        if (progressAtAbort === null && progressData.totalLoaded) {
          progressAtAbort = progressData;
          abortController.abort();
        }
      },
    }),
    /Failed to fetch the model file/,
    "Aborting mid-stream rejects."
  );

  Assert.less(
    progressAtAbort.totalLoaded,
    BODY_SIZE,
    "The abort landed mid-stream rather than after the last chunk."
  );
  Assert.equal(
    hub.requestCount({ ...PINNED_FILE, method: "GET" }),
    1,
    "The body was requested once, and an abort does not retry."
  );

  // TODO - This should not leave an entry behind.
  const entry = await opfsEntry(modelHub, PINNED_FILE);
  info(`OPFS entry after an aborted download: ${JSON.stringify(entry)}`);

  Assert.ok(entry.exists, "An aborted download also leaves an OPFS entry.");
  Assert.equal(entry.size, 0, "The entry is empty.");
  Assert.ok(
    !(await hasHeaders(modelHub, PINNED_FILE)),
    "No headers row was written."
  );

  await cleanup();
});

/**
 * Ensure a "main" revision recovers by re-downloading after an interrupted
 * transfer.
 */
add_task(async function test_retry_after_interrupted_download_on_main() {
  const { hub, modelHub, args, serveInFull, cleanup } = await withModelHub({
    modelFile: MAIN_FILE,
    failAfterBytes: FAIL_AFTER_BYTES,
  });

  await Assert.rejects(
    modelHub.getModelFileAsBlob(args),
    /Failed to fetch the model file/,
    "The first attempt is cut off."
  );
  Assert.equal(
    (await opfsEntry(modelHub, MAIN_FILE)).size,
    0,
    "An empty entry is left behind, the same as for a pinned revision."
  );

  serveInFull();

  const [blob, headers] = await modelHub.getModelFileAsBlob(args);

  Assert.equal(
    blob.size,
    BODY_SIZE,
    "The retry downloads the whole file over the empty entry."
  );
  Assert.equal(
    headers.fileSize,
    BODY_SIZE,
    "The stored file size matches the body that was served."
  );
  Assert.equal(
    hub.requestCount({ ...MAIN_FILE, method: "GET" }),
    2,
    "The retry went back to the network."
  );
  Assert.equal(
    (await opfsEntry(modelHub, MAIN_FILE)).size,
    BODY_SIZE,
    "The OPFS entry holds the full body."
  );

  await cleanup();
});

/**
 * Ensure a pinned revision recovers by re-downloading after an interrupted
 * transfer.
 *
 * TODO: Currently the 0 byte entry reads as a cache hit, so the file is stuck.
 * Every later attempt fails without touching the network, and nothing in the
 * public API clears it.
 */
add_task(async function test_retry_after_interrupted_download_on_pinned() {
  const { hub, modelHub, args, serveInFull, cleanup } = await withModelHub({
    modelFile: PINNED_FILE,
    failAfterBytes: FAIL_AFTER_BYTES,
  });

  await Assert.rejects(
    modelHub.getModelFileAsBlob(args),
    /Failed to fetch the model file/,
    "The first attempt is cut off."
  );

  serveInFull();

  for (const attempt of ["second", "third"]) {
    await Assert.rejects(
      modelHub.getModelFileAsBlob(args),
      /Missing cached headers for the model file/,
      `The ${attempt} attempt fails on the missing headers row instead of ` +
        "downloading the file the hub is now serving in full."
    );
  }

  Assert.equal(
    hub.requestCount({ ...PINNED_FILE, method: "GET" }),
    1,
    "Neither retry reached the network."
  );
  Assert.equal(
    (await opfsEntry(modelHub, PINNED_FILE)).size,
    0,
    "The empty entry responsible for this is still there."
  );

  // deleteModels walks the task store, which an interrupted download never
  // writes to, so the entry is not reachable through the public delete API.
  await modelHub.deleteModels({
    taskName: TASK_NAME,
    model: null,
    revision: null,
  });

  Assert.ok(
    (await opfsEntry(modelHub, PINNED_FILE)).exists,
    "deleteModels leaves the entry in place."
  );

  await cleanup();
});

/**
 * Ensure a hub that answers with a server error surfaces that status instead of
 * caching the error body as model data.
 */
add_task(async function test_server_error_rejects() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: PINNED_FILE,
    failTimes: 10,
  });

  await Assert.rejects(
    modelHub.getModelFileAsBlob(args),
    /HTTP error! Status: 500/,
    "The status the hub answered with reaches the caller."
  );

  Assert.equal(
    hub.requestCount({ ...PINNED_FILE, method: "GET" }),
    1,
    "The body was requested once, and a 500 is not retried."
  );

  const entry = await opfsEntry(modelHub, PINNED_FILE);
  Assert.ok(
    !entry.exists,
    "No OPFS entry was created, because the request failed before a file " +
      "handle was opened."
  );
  Assert.ok(
    !(await hasHeaders(modelHub, PINNED_FILE)),
    "No headers row was written."
  );

  await cleanup();
});

/**
 * Ensure a pinned revision recovers once the hub stops erroring.
 */
add_task(async function test_retry_after_server_error_on_pinned() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: PINNED_FILE,
    failTimes: 1,
  });

  await Assert.rejects(
    modelHub.getModelFileAsBlob(args),
    /HTTP error! Status: 500/,
    "The first attempt gets the server error."
  );

  const [blob, headers] = await modelHub.getModelFileAsBlob(args);

  Assert.equal(
    blob.size,
    BODY_SIZE,
    "The second attempt downloads the whole file."
  );
  Assert.equal(
    headers.fileSize,
    BODY_SIZE,
    "The stored file size matches the body that was served."
  );
  Assert.equal(
    hub.requestCount({ ...PINNED_FILE, method: "GET" }),
    2,
    "The retry went back to the network."
  );
  Assert.equal(
    (await opfsEntry(modelHub, PINNED_FILE)).size,
    BODY_SIZE,
    "The OPFS entry holds the full body."
  );

  await cleanup();
});

/**
 * Ensure a failed revalidation on "main" does not block using a cached file.
 */
add_task(async function test_server_error_on_main_rejects() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: MAIN_FILE,
    failTimes: 10,
  });

  await Assert.rejects(
    modelHub.getModelFileAsBlob(args),
    /HTTP error! Status: 500/,
    "An erroring hub fails the read rather than serving an empty file."
  );

  Assert.deepEqual(
    {
      head: hub.requestCount({ ...MAIN_FILE, method: "HEAD" }),
      get: hub.requestCount({ ...MAIN_FILE, method: "GET" }),
    },
    { head: 1, get: 1 },
    "The read revalidated, got nothing usable, and tried the body anyway."
  );
  Assert.ok(
    !(await opfsEntry(modelHub, MAIN_FILE)).exists,
    "No OPFS entry was created."
  );

  await cleanup();
});
