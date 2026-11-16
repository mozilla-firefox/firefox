/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

// ModelHub treats "main" as a moving branch and anything else as an immutable
// snapshot. These two differ only in revision.
const MAIN_FILE = {
  model: "acme/bert",
  revision: "main",
  file: "config.json",
};

const PINNED_FILE = { ...MAIN_FILE, revision: "v1" };

const TASK_NAME = "test_model_hub_etag";

const ORIGINAL_CONFIG = JSON.stringify({ hidden_size: 32 });
const UPDATED_CONFIG = JSON.stringify({ hidden_size: 64 });

/**
 * @param {ArrayBuffer} buffer
 * @returns {string}
 */
function decode(buffer) {
  return new TextDecoder().decode(new Uint8Array(buffer));
}

/**
 * The requests the hub has seen for a file, split by method. HEAD counts are
 * revalidations and GET counts are body transfers.
 *
 * @param {object} hub
 * @param {object} modelFile
 * @returns {{head: number, get: number}}
 */
function requestCounts(hub, modelFile) {
  return {
    head: hub.requestCount({ ...modelFile, method: "HEAD" }),
    get: hub.requestCount({ ...modelFile, method: "GET" }),
  };
}

/**
 * Stand up a mock hub serving one model file, plus a ModelHub pointed at it.
 * The caller is responsible for calling the returned cleanup.
 *
 * @param {object} options
 * @param {object} options.modelFile - The {model, revision, file} to serve.
 */
async function withModelHub({ modelFile }) {
  const hub = MLTestUtils.createMockModelHub();
  const served = hub.setFile({ ...modelFile, content: ORIGINAL_CONFIG });

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

    /** Publish new bytes under a new ETag, the way a hub update would. */
    publishUpdate() {
      return hub.setFile({ ...modelFile, content: UPDATED_CONFIG });
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
 * Ensure "main" revalidates against the hub on every read, and only transfers
 * the body when it has to.
 */
add_task(async function test_main_revalidates_on_every_read() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: MAIN_FILE,
  });

  for (const read of [1, 2, 3]) {
    const [buffer] = await modelHub.getModelFileAsArrayBuffer(args);

    Assert.deepEqual(
      requestCounts(hub, MAIN_FILE),
      { head: read, get: 1 },
      `Read ${read} sent a HEAD, and only the first read fetched the body.`
    );
    Assert.equal(decode(buffer), ORIGINAL_CONFIG, `Read ${read} is intact.`);
  }

  await cleanup();
});

/**
 * Ensure a pinned revision never revalidates. The revision names an immutable
 * snapshot, so the HEAD would cost a round trip per read and tell us nothing.
 */
add_task(async function test_pinned_never_revalidates() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: PINNED_FILE,
  });

  for (const read of [1, 2, 3]) {
    const [buffer] = await modelHub.getModelFileAsArrayBuffer(args);

    Assert.deepEqual(
      requestCounts(hub, PINNED_FILE),
      { head: 0, get: 1 },
      `Read ${read} went nowhere near the network after the first.`
    );
    Assert.equal(decode(buffer), ORIGINAL_CONFIG, `Read ${read} is intact.`);
  }

  await cleanup();
});

/**
 * Ensure "main" picks up new bytes when the hub's ETag changes.
 */
add_task(async function test_main_redownloads_when_etag_changes() {
  const { hub, modelHub, args, served, publishUpdate, cleanup } =
    await withModelHub({ modelFile: MAIN_FILE });

  const [before, beforeHeaders] =
    await modelHub.getModelFileAsArrayBuffer(args);
  Assert.equal(
    decode(before),
    ORIGINAL_CONFIG,
    "The first read is the old file."
  );
  Assert.equal(beforeHeaders.ETag, served.etag, "The old ETag was stored.");

  const updated = publishUpdate();
  Assert.notEqual(updated.etag, served.etag, "The hub is serving a new ETag.");

  const [after, afterHeaders] = await modelHub.getModelFileAsArrayBuffer(args);

  Assert.deepEqual(
    requestCounts(hub, MAIN_FILE),
    { head: 2, get: 2 },
    "The second read revalidated, saw a different ETag, and refetched."
  );
  Assert.equal(
    decode(after),
    UPDATED_CONFIG,
    "The new bytes replaced the old."
  );
  Assert.equal(afterHeaders.ETag, updated.etag, "The new ETag was stored.");

  await cleanup();
});

/**
 * Ensure a pinned revision keeps serving its cached copy when the hub changes
 * underneath it. This is the point of pinning, but note it is decided purely by
 * the revision not being "main". Nothing checks that the revision actually
 * names an immutable ref, so a moved tag would go unnoticed.
 */
add_task(async function test_pinned_ignores_changed_etag() {
  const { hub, modelHub, args, served, publishUpdate, cleanup } =
    await withModelHub({ modelFile: PINNED_FILE });

  await modelHub.getModelFileAsArrayBuffer(args);

  const updated = publishUpdate();
  Assert.notEqual(updated.etag, served.etag, "The hub is serving a new ETag.");

  const [after, afterHeaders] = await modelHub.getModelFileAsArrayBuffer(args);

  Assert.deepEqual(
    requestCounts(hub, PINNED_FILE),
    { head: 0, get: 1 },
    "The second read never asked the hub, so it could not notice the change."
  );
  Assert.equal(decode(after), ORIGINAL_CONFIG, "The cached bytes are served.");
  Assert.equal(afterHeaders.ETag, served.etag, "The stored ETag is unchanged.");

  await cleanup();
});

/**
 * Ensure "main" falls back to the cached copy when revalidation fails, rather
 * than failing the read. getETag swallows the error and returns null, which
 * counts as "no reason to believe the cache is stale".
 */
add_task(async function test_main_serves_cache_when_revalidation_fails() {
  const { hub, modelHub, args, cleanup } = await withModelHub({
    modelFile: MAIN_FILE,
  });

  await modelHub.getModelFileAsArrayBuffer(args);

  // The hub now 404s, so the HEAD comes back without an ETag.
  hub.removeFile(MAIN_FILE);

  const [after] = await modelHub.getModelFileAsArrayBuffer(args);

  Assert.deepEqual(
    requestCounts(hub, MAIN_FILE),
    { head: 2, get: 1 },
    "The second read revalidated and did not refetch the body."
  );
  Assert.equal(
    decode(after),
    ORIGINAL_CONFIG,
    "A failed revalidation serves the cached file instead of rejecting."
  );

  await cleanup();
});

/**
 * An interrupted update must preserve the cached copy for reads when
 * revalidation is unavailable.
 */
add_task(async function test_main_preserves_cache_after_interrupted_update() {
  const { hub, modelHub, args, served, cleanup } = await withModelHub({
    modelFile: MAIN_FILE,
  });

  info("Cache the original file before the hub publishes an update.");
  const [before, beforeHeaders] =
    await modelHub.getModelFileAsArrayBuffer(args);
  Assert.equal(decode(before), ORIGINAL_CONFIG, "The original file is cached.");
  Assert.equal(beforeHeaders.ETag, served.etag, "The original ETag is cached.");

  info("Publish a new ETag and interrupt the replacement body mid-transfer.");
  const updated = hub.setFile({
    ...MAIN_FILE,
    content: UPDATED_CONFIG.repeat(4096),
    failAfterBytes: 16 * 1024,
  });
  Assert.notEqual(
    updated.etag,
    served.etag,
    "The hub now has a different ETag."
  );

  await Assert.rejects(
    modelHub.getModelFileAsArrayBuffer(args),
    /Failed to fetch the model file/,
    "The interrupted replacement download rejects."
  );
  Assert.deepEqual(
    requestCounts(hub, MAIN_FILE),
    { head: 2, get: 2 },
    "The changed ETag triggered a replacement GET."
  );

  info("Make revalidation fail and read the original cached file.");
  hub.removeFile(MAIN_FILE);
  const [after, afterHeaders] = await modelHub.getModelFileAsArrayBuffer(args);
  Assert.equal(
    decode(after),
    ORIGINAL_CONFIG,
    "The original cached bytes survive the interrupted replacement."
  );
  Assert.equal(
    afterHeaders.ETag,
    served.etag,
    "The original cached ETag is preserved."
  );
  Assert.deepEqual(
    requestCounts(hub, MAIN_FILE),
    { head: 3, get: 2 },
    "Failed revalidation falls back to the cache without another GET."
  );

  await cleanup();
});
