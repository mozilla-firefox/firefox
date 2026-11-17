/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const ECHO_STUB_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/ml_model_hub_echo_stub.worker.mjs";

const TINY_LLAMA = {
  model: "Mozilla/test-llama",
  revision: "main",
  file: "TinyStories-656K.Q8_0.gguf",
};

/**
 * @param {string} name - A file under data/Mozilla/test-llama/main/.
 * @returns {Promise<Uint8Array>}
 */
function readTestModel(name) {
  return IOUtils.read(getTestFilePath(`data/Mozilla/test-llama/main/${name}`));
}

/**
 * Point a mock hub at the engine and tear both down afterwards. The hub serves
 * over localhost, which ModelHub's URLChecker always allows, so no test needs
 * MOZ_ALLOW_EXTERNAL_ML_HUB to reach it.
 *
 * @param {object} [options]
 * @param {Array<[string, boolean|string|number]>} [options.prefs]
 */
async function withHubbedEngine({ prefs = [] } = {}) {
  const hub = MLTestUtils.createMockModelHub();
  const { cleanup: cleanupSetup } = await setup({ prefs });

  await EngineProcess.destroyMLEngine();
  await IndexedDBCache.init({ reset: true });

  return {
    hub,
    engineOptions: {
      modelHubRootUrl: hub.rootUrl,
      modelHubUrlTemplate: hub.urlTemplate,
    },
    async cleanup() {
      await EngineProcess.destroyMLEngine();
      await IndexedDBCache.init({ reset: true });
      await hub.cleanup();
      await cleanupSetup();
    },
  };
}

/**
 * Ensure the ModelHub works correctly through the createEngine interface and a mocked
 * worker that just echos the bytes.
 */
add_task(async function test_chrome_worker_receives_exact_bytes() {
  const { hub, engineOptions, cleanup } = await withHubbedEngine();
  const served = hub.setFile({ ...TINY_LLAMA, content: "not really a model" });

  const workerConfigStub = sinon
    .stub(MLEngineParent, "getWorkerConfig")
    .callsFake(() => ({ url: ECHO_STUB_URL, options: { type: "module" } }));

  try {
    const enginePromise = createEngine({
      engineId: "model-hub-echo",
      taskName: "text-generation",
      // Deliberately not llama.cpp: TextGenerationEngine.shouldRoute would
      // divert to the HWInference process, where this worker stub is inert.
      // onnx-native rather than onnx so no wasm runtime is fetched; only
      // WASM_BACKENDS pulls the attachment, and the stub ignores it anyway.
      backend: "onnx-native",
      modelId: TINY_LLAMA.model,
      modelRevision: TINY_LLAMA.revision,
      modelFile: TINY_LLAMA.file,
      ...engineOptions,
    });

    const engine = await enginePromise;
    const outcome = await engine.run({ args: ["unused"] });

    Assert.equal(outcome.error, null, "The worker fetched the file.");
    Assert.ok(outcome.received, "The worker received a blob.");
    Assert.equal(
      outcome.byteLength,
      served.size,
      "The worker received every byte the hub served."
    );
    Assert.equal(
      outcome.sha256,
      served.contentHash,
      "The bytes are unchanged after crossing into the inference process."
    );
    Assert.equal(
      hub.requestCount({ ...TINY_LLAMA, method: "GET" }),
      1,
      "The file was downloaded exactly once."
    );
  } finally {
    workerConfigStub.restore();
    await cleanup();
  }
});

/**
 * Ensure the ModelHub can serve a real gguf prompt e2e with a mocked ModelHub
 * http endpoint.
 */
add_task(async function test_hwinference_loads_model_served_by_hub() {
  const { hub, engineOptions, cleanup } = await withHubbedEngine({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  const served = hub.setFile({
    ...TINY_LLAMA,
    content: await readTestModel(TINY_LLAMA.file),
  });

  let engine = null;
  try {
    engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "model-hub-hwi",
      ...engineOptions,
    });

    Assert.ok(engine, "The HWInference engine was created from the mock hub.");
    Assert.equal(
      hub.requestCount({ ...TINY_LLAMA, method: "GET" }),
      1,
      "The model was downloaded exactly once."
    );

    const text = await MLTestUtils.gatherText(
      engine.runWithGenerator({
        prompt: TINYSTORIES_STORYTELLER_PROMPT,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
        nPredict: 4,
        minOutputBufferSize: 1,
      })
    );

    Assert.greater(
      text.length,
      0,
      "The model served over HTTP actually generates, so the file descriptor " +
        `reached llama.cpp intact (${served.size} bytes).`
    );
  } finally {
    await engine?.terminate();
    await cleanup();
  }
});

/**
 * Test what happens when the download is interrupted.
 */
add_task(async function test_hwinference_recovers_from_interrupted_download() {
  const { hub, engineOptions, cleanup } = await withHubbedEngine({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  const model = await readTestModel(TINY_LLAMA.file);

  hub.setFile({
    ...TINY_LLAMA,
    content: model,
    failAfterBytes: 128 * 1024,
  });

  let engine = null;
  try {
    await Assert.rejects(
      createEngine({
        ...TINYSTORIES_ENGINE_OPTIONS,
        engineId: "model-hub-hwi-interrupted",
        ...engineOptions,
      }),
      /./,
      "An interrupted download fails instead of loading a partial model."
    );

    // Heal the hub and retry, the way a feature would after a dropped connection.
    hub.setFile({ ...TINY_LLAMA, content: model });

    engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "model-hub-hwi-interrupted",
      ...engineOptions,
    });

    const text = await MLTestUtils.gatherText(
      engine.runWithGenerator({
        prompt: TINYSTORIES_STORYTELLER_PROMPT,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
        nPredict: 4,
        minOutputBufferSize: 1,
      })
    );

    Assert.greater(
      text.length,
      0,
      "The retry produced a working model, so the interrupted attempt left no " +
        "partial file behind that could be mistaken for a complete one."
    );
  } finally {
    await engine?.terminate();
    await cleanup();
  }
});

/**
 * The blob ModelHub returns must be file-backed, since the llama.cpp text
 * generator reads it via a file descriptor rather than loading its bytes.
 * Skips createEngine so a failure points at ModelHub, not the engine stack.
 */
add_task(async function test_model_hub_blob_is_file_backed() {
  const { hub, cleanup } = await withHubbedEngine();
  hub.setFile({ ...TINY_LLAMA, content: await readTestModel(TINY_LLAMA.file) });

  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
    reset: true,
  });

  let generator = null;
  try {
    const [blob] = await modelHub.getModelFileAsBlob({
      taskName: "text-generation",
      model: TINY_LLAMA.model,
      revision: TINY_LLAMA.revision,
      file: TINY_LLAMA.file,
    });

    Assert.ok(Blob.isInstance(blob), "ModelHub returned a blob.");

    // TextGenerator.cpp rejects with "model Blob is not file-backed" unless a
    // file descriptor can be extracted, so resolving here is the assertion.
    generator = await TextGenerator.create(blob, { contextSize: 32 });

    Assert.ok(
      generator,
      "A file descriptor was extracted from the blob ModelHub returned."
    );
  } finally {
    generator?.terminate();
    if (modelHub.cache) {
      await deleteCache(modelHub.cache);
    }
    await cleanup();
  }
});
