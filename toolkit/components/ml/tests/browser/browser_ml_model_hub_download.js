/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const { computeHash } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const MODEL_FILE = {
  model: "acme/bert",
  revision: "main",
  file: "config.json",
};

const MODEL_ARGS = { ...MODEL_FILE, taskName: "test_model_hub_download" };

const CONFIG_JSON = JSON.stringify({ hidden_size: 32 });

/**
 * @param {ArrayBuffer} buffer
 * @returns {string}
 */
function decode(buffer) {
  return new TextDecoder().decode(new Uint8Array(buffer));
}

/**
 * The hex SHA-256 of a buffer, to compare against the `contentHash` the mock
 * hub reports for what it served.
 *
 * @param {ArrayBuffer} buffer
 * @returns {Promise<string>}
 */
function sha256(buffer) {
  return computeHash(new Blob([buffer]), "sha256", "hex");
}

/**
 * Ensure all of the model hubs are cleaned up when a test file finishes.
 */
let gOpenModelHubs = 0;
registerCleanupFunction(() => {
  Assert.equal(
    gOpenModelHubs,
    0,
    "Every withModelHub() context was cleaned up by the task that created it."
  );
});

/**
 * Stand up a mock hub serving one model file, plus a ModelHub pointed at it.
 * The caller is responsible for calling the returned cleanup.
 *
 * @param {object} [options]
 * @param {string} [options.content]
 * @param {?number} [options.pauseAfterBytes] - Send only this many bytes,
 *   then hold the connection open until `hub.resumeAfterPause()` is called.
 * @returns {Promise<object>}
 */
async function withModelHub({
  content = CONFIG_JSON,
  pauseAfterBytes = null,
} = {}) {
  const hub = MLTestUtils.createMockModelHub();
  const served = hub.setFile({ ...MODEL_FILE, content, pauseAfterBytes });

  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
    reset: true,
  });

  let hasCleanedUp = false;
  gOpenModelHubs++;

  return {
    hub,
    modelHub,
    served,
    async cleanup() {
      if (hasCleanedUp) {
        throw new Error("Attempted to cleanup twice.");
      }
      hasCleanedUp = true;
      gOpenModelHubs--;
      if (modelHub.cache) {
        await deleteCache(modelHub.cache);
      }
      await hub.cleanup();
    },
  };
}

/**
 * The happy path: a file the hub serves arrives intact, and arrives over HTTP
 * rather than out of a fixture directory.
 */
add_task(async function test_downloads_file_from_hub() {
  const { hub, modelHub, served, cleanup } = await withModelHub();

  const [buffer, headers] =
    await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    decode(buffer),
    CONFIG_JSON,
    "The bytes the hub served arrived intact."
  );
  Assert.equal(
    JSON.parse(decode(buffer)).hidden_size,
    32,
    "The file parses as the JSON that was served."
  );
  Assert.equal(
    headers.ETag,
    served.etag,
    "The stored headers carry the ETag the hub served."
  );
  Assert.equal(
    headers.fileSize,
    served.size,
    "The stored file size matches the body length."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    1,
    "The file was downloaded exactly once."
  );

  await cleanup();
});

/**
 * A second read is served from the cache. Asserted against the hub's request
 * log, so this fails if anything silently refetches the body.
 */
add_task(async function test_second_read_does_not_refetch_body() {
  const { hub, modelHub, cleanup } = await withModelHub();

  const [first] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);
  const getsAfterFirst = hub.requestCount({ ...MODEL_FILE, method: "GET" });

  const [second] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    decode(second),
    decode(first),
    "The cached read returns the same bytes."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    getsAfterFirst,
    "The body was not downloaded a second time."
  );

  await cleanup();
});

/**
 * A file missing from the hub surfaces as a failure rather than caching an
 * error page as if it were model data.
 */
add_task(async function test_missing_file_rejects() {
  const { hub, modelHub, cleanup } = await withModelHub();
  hub.removeFile(MODEL_FILE);

  await Assert.rejects(
    modelHub.getModelFileAsArrayBuffer(MODEL_ARGS),
    /404/,
    "Requesting a file the hub does not have rejects."
  );

  await cleanup();
});

/**
 * Two callers asking for the same file at the same time. Nothing dedupes the
 * in-flight request, so this characterizes what that costs and what it does
 * not break.
 */
add_task(async function test_concurrent_reads_download_twice() {
  // Large enough that the two transfers and their OPFS writes genuinely
  // overlap, rather than the first finishing before the second starts.
  const content = "x".repeat(1024 * 1024);
  const { hub, modelHub, served, cleanup } = await withModelHub({ content });

  const [[firstBuffer, firstHeaders], [secondBuffer, secondHeaders]] =
    await Promise.all([
      modelHub.getModelFileAsArrayBuffer(MODEL_ARGS),
      modelHub.getModelFileAsArrayBuffer(MODEL_ARGS),
    ]);

  // TODO: ModelHub keeps no map of in-flight downloads, so N simultaneous
  // callers means N transfers of the same bytes.
  Assert.deepEqual(
    {
      head: hub.requestCount({ ...MODEL_FILE, method: "HEAD" }),
      get: hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    },
    { head: 2, get: 2 },
    "Both callers revalidated and both downloaded the body."
  );

  Assert.equal(
    firstBuffer.byteLength,
    served.size,
    "The first caller got the whole file."
  );
  Assert.equal(
    secondBuffer.byteLength,
    served.size,
    "The second caller got the whole file."
  );

  // Compared by digest so a failure does not print a megabyte of diff.
  Assert.equal(
    await sha256(firstBuffer),
    served.contentHash,
    "The first caller's bytes are intact despite the overlapping write."
  );
  Assert.equal(
    await sha256(secondBuffer),
    served.contentHash,
    "The second caller's bytes are intact despite the overlapping write."
  );

  Assert.equal(firstHeaders.ETag, served.etag, "The first caller got an ETag.");
  Assert.equal(
    secondHeaders.ETag,
    served.etag,
    "The second caller got an ETag."
  );
  Assert.equal(
    secondHeaders.fileSize,
    served.size,
    "The file size stored by the write that landed last is the full size."
  );

  // Each caller writes through its own createWritable, which swaps a complete
  // temporary file into place on close, so the loser of the race is overwritten
  // whole rather than interleaved with the winner.
  const [after] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    await sha256(after),
    served.contentHash,
    "The file left in the cache is intact."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    2,
    "The cached file is good enough to serve a later read without refetching."
  );

  await cleanup();
});
