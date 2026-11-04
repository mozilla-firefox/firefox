/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const MOZ_ECHO_OPTIONS_RAW = { taskName: "moz-echo", timeoutMS: -1 };
const MOZ_ECHO_OPTIONS = new PipelineOptions({
  taskName: "moz-echo",
  timeoutMS: -1,
});

function getRunResponse(responseSpy) {
  const responseCall = responseSpy
    .getCalls()
    .find(call => call.args[0].data.type === "EnginePort:RunResponse");
  return responseCall?.args[0].data;
}

async function waitForRunResponse(responseSpy) {
  await TestUtils.waitForCondition(
    () => getRunResponse(responseSpy),
    "The child sends the canceled request's response",
    100,
    100
  );
  return getRunResponse(responseSpy);
}

async function checkCanceledRunWithLateResponse(request) {
  const { cleanup, remoteClients } = await setup();
  let responseSpy;

  try {
    const engine = await createEngine(MOZ_ECHO_OPTIONS_RAW);
    const warmup = engine.run({ data: "Initialize the engine" });
    await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
    await warmup;

    responseSpy = sinon.spy(engine, "handlePortMessage");
    const successCount =
      Glean.firefoxAiRuntime.runInferenceSuccessFlow.testGetValue()?.length ??
      0;
    const failureCount =
      Glean.firefoxAiRuntime.runInferenceFailure.testGetValue()?.length ?? 0;
    const controller = new AbortController();
    const reason = new DOMException("Canceled by the caller", "AbortError");
    const pending = engine.run(request, { signal: controller.signal });

    controller.abort(reason);

    await Assert.rejects(
      pending,
      error => error === reason,
      "The caller receives the original cancellation reason without waiting for the backend"
    );
    Assert.ok(
      !getRunResponse(responseSpy),
      "The caller is released before the initialized backend sends its response"
    );
    const lateResponse = await waitForRunResponse(responseSpy);
    Assert.equal(
      Glean.firefoxAiRuntime.runInferenceSuccessFlow.testGetValue()?.length ??
        0,
      successCount,
      "The canceled request does not record a success event"
    );
    Assert.equal(
      Glean.firefoxAiRuntime.runInferenceFailure.testGetValue()?.length ?? 0,
      failureCount,
      "The canceled request does not record a backend failure event"
    );

    const next = await engine.run({ data: "Next request" });
    Assert.equal(
      next.output.echo,
      "Next request",
      "A new request receives its own response after cancellation"
    );
    Assert.equal(
      Glean.firefoxAiRuntime.runInferenceSuccessFlow.testGetValue()?.length ??
        0,
      successCount + 1,
      "Only the new request records a success event"
    );
    return lateResponse;
  } finally {
    await EngineProcess.destroyMLEngine()
      .then(() => cleanup())
      .finally(() => responseSpy?.restore());
  }
}

add_task(
  async function test_canceled_run_does_not_start_after_initialization() {
    const { cleanup, remoteClients } = await setup();
    let responseSpy;

    try {
      const engine = await createEngine(MOZ_ECHO_OPTIONS_RAW);
      responseSpy = sinon.spy(engine, "handlePortMessage");
      const controller = new AbortController();
      const reason = new DOMException(
        "Canceled during initialization",
        "AbortError"
      );
      const pending = engine.run("throw", { signal: controller.signal });

      controller.abort(reason);

      await Assert.rejects(
        pending,
        error => error === reason,
        "The caller receives cancellation while initialization is pending"
      );
      Assert.ok(
        !getRunResponse(responseSpy),
        "The runtime download is still pending"
      );

      await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
      const canceledResponse = await waitForRunResponse(responseSpy);
      Assert.equal(
        canceledResponse.error?.name,
        "AbortError",
        "The child cancels the request before the worker can throw its execution error"
      );
      Assert.equal(
        canceledResponse.response,
        null,
        "The canceled request does not produce an inference result"
      );

      const next = await engine.run({ data: "Next request" });
      Assert.equal(
        next.output.echo,
        "Next request",
        "The initialized engine can execute another request after cancellation"
      );
    } finally {
      await EngineProcess.destroyMLEngine()
        .then(() => cleanup())
        .finally(() => responseSpy?.restore());
    }
  }
);

add_task(async function test_canceled_run_ignores_late_success() {
  const { response, error } = await checkCanceledRunWithLateResponse({
    data: "Canceled request",
  });
  Assert.ok(!error, "The backend completes the request despite cancellation");
  Assert.equal(
    response.output.echo,
    "Canceled request",
    "The late response belongs to the canceled request"
  );
});

add_task(async function test_canceled_run_ignores_late_backend_error() {
  const { error } = await checkCanceledRunWithLateResponse("throw");
  Assert.ok(
    error?.message.includes('Received the message "throw"'),
    "The backend error arrives after the caller has already received cancellation"
  );
});

async function waitForEngineStatus(engineInstance, status) {
  await TestUtils.waitForCondition(async () => {
    let statusByEngineId = new Map(
      await engineInstance.mlEngineParent.getStatusByEngineId()
    );
    return statusByEngineId.get(engineInstance.engineId)?.status == status;
  }, `Engine is ${status}`);
}

async function killInferenceProcess() {
  let { osPid } = ChromeUtils.getAllDOMProcesses().find(
    process => process.remoteType == "inference"
  );
  let mm;
  for (let i = 0; i < Services.ppmm.childCount; i++) {
    if (Services.ppmm.getChildAt(i).osPid == osPid) {
      mm = Services.ppmm.getChildAt(i);
      break;
    }
  }
  Assert.ok(mm, "Inference process message manager");
  await new Promise(resolve => {
    mm.addMessageListener("MLTest:IntentionalCrash", function listener() {
      mm.removeMessageListener("MLTest:IntentionalCrash", listener);
      resolve();
    });
    mm.loadProcessScript(
      "data:,ChromeUtils.privateNoteIntentionalCrash();" +
        "sendAsyncMessage('MLTest:IntentionalCrash');",
      false
    );
  });
  Cc["@mozilla.org/processtools-service;1"]
    .getService(Ci.nsIProcessToolsService)
    .kill(osPid);
}

/**
 * Performing a basic engine initialization and run.
 */
add_task(async function test_ml_engine_basics() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);

  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  const res = await inferencePromise;
  Assert.equal(
    res.output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.equal(res.output.dtype, "q8", "The config was enriched by RS");
  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

/**
 * Test the Wasm failing to download triggering a rejection.
 */
add_task(async function test_ml_engine_wasm_rejection() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].rejectPendingDownloads(1);

  let error;
  try {
    await inferencePromise;
  } catch (e) {
    error = e;
  }

  is(
    error?.message,
    "Intentionally rejecting downloads.",
    "The error is correctly surfaced."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Make sure we don't get race conditions when running several inference runs in parallel
 */
add_task(async function test_ml_engine_parallel() {
  const { cleanup, remoteClients } = await setup();

  // We're doing 10 calls and each echo call will take from 0 to 1000ms
  // So we're sure we're mixing runs.
  let sleepTimes = [300, 1000, 700, 0, 500, 900, 400, 800, 600, 100];
  let numCalls = 10;

  const enginesSeen = new Set();
  async function run(x) {
    const engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);
    enginesSeen.add(engineInstance);

    let msg = `${x} - This gets echoed.`;
    let res = engineInstance.run({
      data: msg,
      sleepTime: sleepTimes[x],
    });

    await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
    res = await res;

    return res;
  }

  info(`Run ${numCalls} inferences in parallel`);
  let runs = [];
  for (let x = 0; x < numCalls; x++) {
    runs.push(run(x));
  }

  // await all runs
  const results = await Promise.all(runs);
  Assert.equal(results.length, numCalls, `All ${numCalls} were successful`);

  // check that each one got their own stuff
  for (let y = 0; y < numCalls; y++) {
    Assert.equal(
      results[y].output.echo,
      `${y} - This gets echoed.`,
      `Result ${y} is correct`
    );
  }

  Assert.equal(enginesSeen.size, 1, "Only one engine was created.");

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

/**
 * Tests that the engineInstanceModel's internal errors are correctly surfaced.
 */
add_task(async function test_ml_engine_model_error() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);

  info("Run the inference with a throwing example.");
  const inferencePromise = engineInstance.run("throw");

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  let error;
  try {
    await inferencePromise;
  } catch (e) {
    error = e;
  }
  is(
    error?.message,
    'Error: Received the message "throw", so intentionally throwing an error.',
    "The error is correctly surfaced."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * This test is really similar to the "basic" test, but tests manually destroying
 * the engineInstance.
 */
add_task(async function test_ml_engine_destruction() {
  const { cleanup, remoteClients } = await setup();

  info("Get engineInstance");
  const engineInstance = await createEngine(MOZ_ECHO_OPTIONS);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await engineInstance.terminate(
    /* shutDownIfEmpty */ true,
    /* replacement */ false
  );

  info(
    "The engineInstance is manually destroyed. The cleanup function should wait for the engine process to be destroyed."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests creating an engine after an error.
 */
add_task(async function test_ml_engine_model_error() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);

  info("Run the inference with a throwing example.");
  const inferencePromise = engineInstance.run("throw");

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  let error;
  try {
    await inferencePromise;
  } catch (e) {
    error = e;
  }
  is(
    error?.message,
    'Error: Received the message "throw", so intentionally throwing an error.',
    "The error is correctly surfaced."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that we display a nice error message when the "browser.ml.enable" pref is off.
 */
add_task(async function test_pref_is_off() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.enable", false]],
  });

  info("Get the engine process");
  let error;

  try {
    await EngineProcess.getMLEngineParent();
  } catch (e) {
    error = e;
  }
  is(
    error?.message,
    "MLEngine is disabled. Check the browser.ml prefs.",
    "The error is correctly surfaced."
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.enable", true]],
  });
});

/**
 * Tests that the engine is reused.
 */
add_task(async function test_ml_engine_reuse_same() {
  const { cleanup, remoteClients } = await setup();

  const options = { taskName: "moz-echo", engineId: "echo" };
  const engineInstance = await createEngine(options);
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  let engineInstance2 = await createEngine(options);
  is(engineInstance2.engineId, "echo", "The engine ID matches");
  is(engineInstance, engineInstance2, "The engine is reused.");
  const inferencePromise2 = engineInstance2.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise2).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that engines are reused when only per-request metadata differs.
 * Telemetry and lifetime fields (featureId, flowId, timeoutMS) must not
 * cause engine replacement, or concurrent callers would interrupt each
 * other's in-flight streams.
 */
add_task(async function test_ml_engine_reuse_metadata_differs() {
  const { cleanup, remoteClients } = await setup();

  const engineInstance = await createEngine({
    taskName: "moz-echo",
    engineId: "echo-metadata",
    featureId: "test-feature",
    flowId: "flow-1",
    timeoutMS: 1000,
  });
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "First inference completes."
  );

  const engineInstance2 = await createEngine({
    taskName: "moz-echo",
    engineId: "echo-metadata",
    featureId: "test-feature",
    flowId: "flow-2",
    timeoutMS: 5000,
  });
  is(
    engineInstance,
    engineInstance2,
    "Engine is reused when only per-request metadata differs."
  );

  const inferencePromise2 = engineInstance2.run({ data: "Echoed again." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
  Assert.equal(
    (await inferencePromise2).output.echo,
    "Echoed again.",
    "Second inference completes on the reused engine."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that we can have two competing engines
 */
add_task(async function test_ml_two_engines() {
  const { cleanup, remoteClients } = await setup();

  const engineInstance = await createEngine({
    taskName: "moz-echo",
    engineId: "engine1",
  });
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  let engineInstance2 = await createEngine({
    taskName: "moz-echo",
    engineId: "engine2",
  });

  const inferencePromise2 = engineInstance2.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise2).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.notEqual(
    engineInstance.engineId,
    engineInstance2.engineId,
    "Should be different engines"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that we can have the same engine reinitialized
 */
add_task(async function test_ml_dupe_engines() {
  const { cleanup, remoteClients } = await setup();

  const engineInstance = await createEngine({
    taskName: "moz-echo",
    engineId: "engine1",
  });
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  let engineInstance2 = await createEngine({
    taskName: "moz-echo",
    engineId: "engine1",
    numThreads: 2, // engine-identity change forces re-creation
  });
  const inferencePromise2 = engineInstance2.run({ data: "This gets echoed." });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise2).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.notEqual(
    engineInstance,
    engineInstance2,
    "Should be different engines"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that a worker can have an infinite timeout.
 */
add_task(async function test_ml_engine_infinite_worker() {
  const { cleanup, remoteClients } = await setup();

  const options = { taskName: "moz-echo", timeoutMS: -1 };
  const engineInstance = await createEngine(options);

  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  const res = await inferencePromise;
  Assert.equal(
    res.output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.equal(res.output.timeoutMS, -1, "This should be an infinite worker.");
  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

/**
 * Tests that a run lasting longer than the keep alive timeout completes.
 */
add_task(async function test_ml_engine_run_outlasts_timeout() {
  const { cleanup, remoteClients } = await setup();

  const engineInstance = await createEngine({
    taskName: "moz-echo",
    timeoutMS: 100,
  });
  const inferencePromise = engineInstance.run({
    data: "This gets echoed.",
    sleepTime: 1000,
  });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "Echo"
  );

  await TestUtils.waitForCondition(
    () => EngineProcess.areAllEnginesTerminated(),
    "Idle engine terminated"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that overlapping runs both complete when they outlast the keep alive timeout.
 */
add_task(async function test_ml_engine_overlapping_runs_outlast_timeout() {
  let { cleanup, remoteClients } = await setup();

  let engineInstance = await createEngine({
    taskName: "moz-echo",
    timeoutMS: 100,
  });
  let sleepTimes = [200, 1000];
  let runs = sleepTimes.map(sleepTime =>
    engineInstance.run({
      data: `${sleepTime} - This gets echoed.`,
      sleepTime,
    })
  );
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  let results = await Promise.all(runs);
  for (let [index, sleepTime] of sleepTimes.entries()) {
    Assert.equal(
      results[index].output.echo,
      `${sleepTime} - This gets echoed.`,
      `Echo ${sleepTime}`
    );
  }

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that terminating an engine rejects the runs in progress.
 */
add_task(async function test_ml_engine_terminate_rejects_runs() {
  let { cleanup, remoteClients } = await setup();

  let engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);
  let inferencePromise = engineInstance.run({
    data: "This gets echoed.",
    sleepTime: 5000,
  });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
  await waitForEngineStatus(engineInstance, "RUNNING");

  await engineInstance.terminate();
  await Assert.rejects(inferencePromise, /terminated/, "Terminated run");

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Tests that an inference process crash rejects the runs in progress.
 */
add_task(async function test_ml_engine_crash_rejects_runs() {
  let { cleanup, remoteClients } = await setup();

  let engineInstance = await createEngine(MOZ_ECHO_OPTIONS_RAW);
  let inferencePromise = engineInstance.run({
    data: "This gets echoed.",
    sleepTime: 5000,
  });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
  await waitForEngineStatus(engineInstance, "RUNNING");

  await killInferenceProcess();
  await Assert.rejects(
    inferencePromise,
    /inference process was shutdown/,
    "Crashed run"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * These status are visualized in about:inference, but aren't used for business
 * logic.
 */
add_task(async function test_ml_engine_get_status_by_engine_id() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine({ taskName: "moz-echo" });

  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  const res = await inferencePromise;
  Assert.equal(
    res.output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  const expected = {
    "default-engine": {
      status: "IDLE",
      options: {
        useExternalDataFormat: false,
        engineId: "default-engine",
        featureId: null,
        taskName: "moz-echo",
        timeoutMS: 1000,
        modelHubRootUrl:
          "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data",
        modelHubUrlTemplate: "{model}/{revision}",
        modelId: "mozilla/distilvit",
        modelRevision: "main",
        flowId: null,
        tokenizerId: "mozilla/distilvit",
        tokenizerRevision: "main",
        processorId: "mozilla/distilvit",
        processorRevision: "main",
        logLevel: "All",
        runtimeFilename: "ort-wasm-simd-threaded.jsep.wasm",
        staticEmbeddingsOptions: null,
        device: null,
        dtype: "q8",
        numThreads: "NOT_COMPARED",
        executionPriority: null,
        kvCacheDtype: null,
        numContext: 1024,
        numBatch: 2048,
        numUbatch: 512,
        flashAttn: false,
        useMmap: false,
        useMlock: true,
        numThreadsDecoding: null,
        modelFile: null,
        backend: null,
        modelHub: null,
        baseURL: null,
        apiKey: null,
        extraHeaders: null,
        serviceType: null,
        purpose: null,
        maxRetries: null,
      },
    },
  };

  const statusByEngineId = Object.fromEntries(
    await engineInstance.mlEngineParent.getStatusByEngineId()
  );
  statusByEngineId["default-engine"].options.numThreads = "NOT_COMPARED";
  Assert.deepEqual(statusByEngineId, expected);

  await ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

add_task(
  async function test_deletePreviousModelRevisions_cleans_stale_revision() {
    const { cleanup } = await setup();

    const mlEngineParent = await EngineProcess.getMLEngineParent();

    const FAKE_HUB =
      "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data";
    const FAKE_URL_TEMPLATE = "{model}/resolve/{revision}";
    const TASK_NAME = "regress-2038342-task";

    await mlEngineParent.getModelFile({
      engineId: "regress-2038342",
      taskName: TASK_NAME,
      url: `${FAKE_HUB}/acme/bert/resolve/main/config.json`,
      rootUrl: FAKE_HUB,
      urlTemplate: FAKE_URL_TEMPLATE,
      featureId: TASK_NAME,
      sessionId: "regress-2038342-session",
    });

    const hub = mlEngineParent.modelHub;
    ok(hub, "ModelHub is initialized after getModelFile");
    const hostname = new URL(FAKE_HUB).hostname;
    const modelWithHostname = `${hostname}/acme/bert`;

    await hub.cache.put({
      taskName: TASK_NAME,
      model: modelWithHostname,
      revision: "stale-revision",
      file: "config.json",
      data: new Blob(["stale-payload"]),
      headers: { ETag: "STALE_ETAG" },
    });

    const stalePre = await hub.cache.getFile({
      model: modelWithHostname,
      revision: "stale-revision",
      file: "config.json",
    });
    Assert.notEqual(stalePre, null, "Stale revision exists before cleanup");

    await mlEngineParent.deletePreviousModelRevisions();

    const stalePost = await hub.cache.getFile({
      model: modelWithHostname,
      revision: "stale-revision",
      file: "config.json",
    });
    Assert.equal(
      stalePost,
      null,
      "Stale revision must be deleted by deletePreviousModelRevisions"
    );

    await hub.cache.dispose();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
);
