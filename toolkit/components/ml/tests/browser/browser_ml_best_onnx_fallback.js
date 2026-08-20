/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const WORKER_STUB_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/ml_best_onnx_fallback_stub.worker.mjs";

// These shared stubs force the availability probe to report native as absent /
// throwing, and mock getBackend to succeed so an engine builds and we can read
// the resolved best-onnx backend.
const NATIVE_UNAVAILABLE_STUB_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/ml_native_ort_unavailable_stub.worker.mjs";

const NATIVE_ERROR_STUB_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/ml_native_ort_error_stub.worker.mjs";

const BEST_ONNX_OPTIONS = {
  taskName: "text-classification",
  modelId: "acme/bert",
  dtype: "q8",
  backend: "best-onnx",
  modelHubUrlTemplate: "{model}/resolve/{revision}",
};

/**
 * Stubs getWorkerConfig to a worker whose availability probe reports the native
 * runtime as present but whose onnx-native engine creation rejects with the
 * message InferenceSession.cpp uses when libonnxruntime is missing. This drives
 * the probe -> native attempt -> wasm fallback path. Returns a cleanup function
 * the caller must invoke in `finally`.
 */
function stubNativeUnavailable() {
  const workerConfigStub = sinon
    .stub(MLEngineParent, "getWorkerConfig")
    .callsFake(() => ({ url: WORKER_STUB_URL, options: { type: "module" } }));
  return () => {
    workerConfigStub.restore();
  };
}

/**
 * Stubs getWorkerConfig to a worker whose availability probe reports the native
 * runtime as absent. Both backends succeed, so a correct probe resolves to wasm
 * onnx; a regression that attempted onnx-native anyway would resolve to
 * "onnx-native" and fail the caller's assertion. Returns a cleanup function.
 */
function stubProbeUnavailable() {
  const workerConfigStub = sinon
    .stub(MLEngineParent, "getWorkerConfig")
    .callsFake(() => ({
      url: NATIVE_UNAVAILABLE_STUB_URL,
      options: { type: "module" },
    }));
  return () => {
    workerConfigStub.restore();
  };
}

/**
 * Stubs getWorkerConfig to a worker whose availability probe throws. best-onnx
 * is expected to swallow the failure and fall back to optimistically trying
 * onnx-native (which succeeds in this stub). Returns a cleanup function.
 */
function stubProbeError() {
  const workerConfigStub = sinon
    .stub(MLEngineParent, "getWorkerConfig")
    .callsFake(() => ({
      url: NATIVE_ERROR_STUB_URL,
      options: { type: "module" },
    }));
  return () => {
    workerConfigStub.restore();
  };
}

/**
 * Verifies best-onnx falls back to the wasm onnx backend when the native
 * runtime fails to load. Against a cold cache the probe reports native as
 * available, so best-onnx attempts onnx-native; the stub rejects that attempt
 * and the dispatcher's catch + retry path runs with backend "onnx", which we
 * observe on the parent-side MLEngine.
 *
 * A non-mocked task (text-classification) is required so the child awaits
 * dispatcher.isReady() and runs initializeInferenceEngine; moz-echo would
 * short-circuit that path and the stub would never be reached.
 */
add_task(async function test_best_onnx_falls_back_to_wasm() {
  const { cleanup, remoteClients } = await setup();
  const restoreStub = stubNativeUnavailable();

  try {
    const enginePromise = createEngine(BEST_ONNX_OPTIONS);

    // Only the wasm retry pulls the runtime; the native attempt is stubbed
    // to throw before getWasmArrayBuffer is reached.
    await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

    const engine = await enginePromise;

    Assert.equal(
      engine.pipelineOptions.backend,
      "onnx",
      "best-onnx falls back to wasm onnx when the native runtime is missing."
    );
  } finally {
    restoreStub();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

/**
 * Repeated best-onnx requests with the same options must reuse the existing
 * engine even when "best-onnx" resolved via the wasm fallback path. Before
 * PipelineOptions.equals understood the sentinel, the cached engine's
 * backend ("onnx") never equalled the new request's backend ("best-onnx").
 */
add_task(async function test_best_onnx_engine_is_reused_after_fallback() {
  const { cleanup, remoteClients } = await setup();
  const restoreStub = stubNativeUnavailable();

  try {
    const enginePromise1 = createEngine(BEST_ONNX_OPTIONS);
    await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
    const engine1 = await enginePromise1;

    const engine2 = await createEngine(BEST_ONNX_OPTIONS);

    Assert.strictEqual(
      engine1,
      engine2,
      "Repeated best-onnx createEngine returns the cached engine instance."
    );
  } finally {
    restoreStub();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

/**
 * When the availability probe reports the native runtime as absent, best-onnx
 * must resolve straight to wasm onnx without attempting onnx-native — avoiding
 * the wasted native pipeline build (Bug 2063023). The stub's onnx-native path
 * succeeds, so a regression that attempted it anyway would resolve the engine
 * to "onnx-native" and fail this assertion.
 */
add_task(async function test_best_onnx_probe_skips_native_when_unavailable() {
  const { cleanup, remoteClients } = await setup();
  const restoreStub = stubProbeUnavailable();

  try {
    const enginePromise = createEngine(BEST_ONNX_OPTIONS);
    await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
    const engine = await enginePromise;

    Assert.equal(
      engine.pipelineOptions.backend,
      "onnx",
      "best-onnx resolves to wasm onnx when the probe reports native as absent."
    );
  } finally {
    restoreStub();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

/**
 * When the availability probe itself throws, best-onnx must not reject: it
 * falls back to optimistically trying onnx-native and lets the engine-creation
 * path resolve the outcome. onnx-native succeeds in this stub, so the engine
 * resolves to "onnx-native" — a value the probe-false path can never produce,
 * which confirms the probe-failure branch of chooseBestBackend ran.
 */
add_task(async function test_best_onnx_probe_error_falls_back_to_native() {
  const { cleanup } = await setup();
  const restoreStub = stubProbeError();

  try {
    // The optimistic onnx-native attempt succeeds in the stub without fetching
    // wasm or a model, so there are no pending downloads to resolve.
    const engine = await createEngine(BEST_ONNX_OPTIONS);

    Assert.equal(
      engine.pipelineOptions.backend,
      "onnx-native",
      "A failed probe falls back to optimistically trying onnx-native."
    );
  } finally {
    restoreStub();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

/**
 * Concurrent cold-cache best-onnx requests must share a single availability
 * probe. chooseBestBackend checks and sets the in-flight promise in one
 * synchronous section, so a second concurrent caller can never launch its own
 * probe. The two requests need distinct engineIds, otherwise MLEngineParent
 * guards creation by engineId and the second waits for the first instead of
 * racing it. Each engine build calls getWorkerConfig once; the shared probe
 * adds exactly one more, so the stub is hit three times (a missing dedup would
 * probe twice, for four hits).
 */
add_task(
  async function test_best_onnx_probe_runs_once_for_concurrent_requests() {
    const { cleanup, remoteClients } = await setup();
    const workerConfigStub = sinon
      .stub(MLEngineParent, "getWorkerConfig")
      .callsFake(() => ({
        url: NATIVE_UNAVAILABLE_STUB_URL,
        options: { type: "module" },
      }));

    try {
      const enginePromise1 = createEngine({
        ...BEST_ONNX_OPTIONS,
        engineId: "best-onnx-concurrent-1",
      });
      const enginePromise2 = createEngine({
        ...BEST_ONNX_OPTIONS,
        engineId: "best-onnx-concurrent-2",
      });

      // One wasm runtime download per engine build.
      await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(2);

      const [engine1, engine2] = await Promise.all([
        enginePromise1,
        enginePromise2,
      ]);

      Assert.equal(
        engine1.pipelineOptions.backend,
        "onnx",
        "First resolves onnx."
      );
      Assert.equal(
        engine2.pipelineOptions.backend,
        "onnx",
        "Second resolves onnx."
      );
      Assert.equal(
        workerConfigStub.callCount,
        3,
        "One shared availability probe plus one build per engine (dedup)."
      );
    } finally {
      workerConfigStub.restore();
      await EngineProcess.destroyMLEngine();
      await cleanup();
    }
  }
);
