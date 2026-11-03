/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// One task per telemetry contract of the HWInference text generation path:
// what a creation, a run and a stream record in firefox.ai.runtime, then what
// each failure mode records. Creation failures record the error name, run
// failures the message, and each mode must come out as its own value.
//
// Not covered, for lack of an injection point: the process refusing to launch,
// the process dying before the model load reports, and the backend refusing a
// model (llama.cpp aborts on an unreadable file instead of reporting).

const HWI_PREFS = [["browser.ml.llama.hwInference", true]];

const { LLAMA_CPP_VERSION } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs"
);

const RUN_REQUEST = {
  prompt: TINYSTORIES_STORYTELLER_PROMPT,
  samplers: TINYSTORIES_GREEDY_SAMPLERS,
  nPredict: 16,
};

function events(name) {
  return Glean.firefoxAiRuntime[name].testGetValue() ?? [];
}

function only(name) {
  const all = events(name);
  Assert.equal(all.length, 1, `Exactly one ${name} event`);
  return all[0].extra;
}

function labeledCount(name, label) {
  return Glean.firefoxAiRuntime[name][label].testGetValue()?.count ?? 0;
}

function lastError(name) {
  const all = events(name);
  return all[all.length - 1]?.extra.error;
}

// crash-me.gguf takes the process down mid-generation. The harness counts
// the minidump as a failure unless the test claims it, as browser_ml_native
// does for the same model.
async function removeCrashDump(shutdown) {
  const [subject] = await shutdown;
  const props = subject.QueryInterface(Ci.nsIPropertyBag2);
  if (!props.hasKey("dumpID")) {
    return;
  }
  const dumpID = props.getPropertyAsAString("dumpID");
  await Services.crashmanager.ensureCrashIsPresent(dumpID);
  const minidumps = Services.dirsvc.get("ProfD", Ci.nsIFile);
  minidumps.append("minidumps");
  for (const suffix of [".dmp", ".extra"]) {
    const file = minidumps.clone();
    file.append(dumpID + suffix);
    if (file.exists()) {
      file.remove(false);
    }
  }
}

add_task(async function test_creation_records_success_and_download() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  await IndexedDBCache.init({ reset: true });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-create",
      featureId: "link-preview",
    });
    await engine.terminate();

    const created = only("engineCreationSuccessFlow");
    Assert.equal(created.engineId, "hwi-tel-create");
    Assert.equal(created.host_process, "hwinference");
    Assert.greaterOrEqual(Number(created.duration), 0);
    Assert.equal(
      labeledCount("engineCreationSuccess", "hwi-tel-create"),
      1,
      "The labeled creation timing got its sample"
    );
    Assert.equal(events("engineCreationFailure").length, 0);

    Assert.deepEqual(
      events("modelDownload").map(e => e.extra.step),
      [
        "start_download",
        "start_file_download",
        "end_file_download_success",
        "end_download_success",
      ],
      "The model download reports each step"
    );
  } finally {
    await cleanup();
  }
});

add_task(async function test_run_records_the_inference_flow() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-run",
      featureId: "link-preview",
    });
    const { metrics } = await engine.run(RUN_REQUEST);
    await engine.terminate();

    Assert.greater(metrics.outputTokens, 0, "The run generated");
    const flow = only("runInferenceSuccessFlow");
    Assert.equal(flow.host_process, "hwinference");
    Assert.equal(Number(flow.input_tokens), metrics.inputTokens);
    Assert.equal(Number(flow.output_tokens), metrics.outputTokens);
    Assert.equal(
      Number(flow.inference_time),
      Math.round(metrics.inferenceTime)
    );
    Assert.equal(Number(flow.decoding_time), Math.round(metrics.decodingTime));
    Assert.equal(
      Number(flow.time_to_first_token),
      Math.round(metrics.timeToFirstToken)
    );
    Assert.equal(
      Number(flow.tokens_per_second),
      Math.round(metrics.tokensPerSecond * 100) / 100
    );
    Assert.equal(
      Number(flow.time_per_output_token),
      Math.round(metrics.timePerOutputToken * 100) / 100
    );
    Assert.equal(
      flow.tokenizing_time,
      undefined,
      "There is no tokenizing phase on this path"
    );
    Assert.equal(
      labeledCount("runInferenceSuccess", "hwi-tel-run"),
      1,
      "The labeled run timing got its sample"
    );
    Assert.equal(events("runInferenceFailure").length, 0);
  } finally {
    await cleanup();
  }
});

add_task(async function test_run_records_the_engine_run() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-resources",
      featureId: "link-preview",
    });
    await engine.run(RUN_REQUEST);
    await engine.terminate();

    const run = only("engineRun");
    Assert.equal(run.feature_id, "link-preview");
    Assert.equal(run.engine_id, "hwi-tel-resources");
    Assert.equal(run.model_id, "Mozilla/test-llama");
    Assert.equal(run.backend, "llama.cpp");
    Assert.equal(run.host_process, "hwinference");
    Assert.equal(
      run.backend_source_revision,
      LLAMA_CPP_VERSION,
      "The run names the llama.cpp revision, as the MLEngine path does"
    );

    for (const key of ["wall_milliseconds", "cores", "memory_bytes"]) {
      Assert.greater(Number(run[key]), 0, `${key} is measured`);
    }
    for (const key of ["cpu_milliseconds", "cpu_utilization"]) {
      Assert.notEqual(run[key], null, `${key} is measured`);
      Assert.greaterOrEqual(Number(run[key]), 0, `${key} is a count`);
    }
    Assert.greater(Number(run.system_memory_mb), 0);

    for (const key of [
      "token_count",
      "character_count",
      "time_to_first_chunk",
      "average_chunk_time",
    ]) {
      Assert.equal(run[key], null, `${key} is only reported for a stream`);
    }
  } finally {
    await cleanup();
  }
});

add_task(async function test_stream_records_the_chunk_metrics() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-stream",
      featureId: "link-preview",
    });

    let text = "";
    let chunks = 0;
    for await (const chunk of engine.runWithGenerator({
      ...RUN_REQUEST,
      minOutputBufferSize: 1,
    })) {
      text += chunk.text;
      chunks += chunk.text ? 1 : 0;
    }
    Assert.greater(chunks, 1, "The stream came in several chunks");

    const [run] = events("engineRun");
    const [flow] = events("runInferenceSuccessFlow");
    Assert.equal(
      Number(run.extra.token_count),
      Number(flow.extra.output_tokens),
      "token_count is the generated token count"
    );
    Assert.equal(
      Number(run.extra.character_count),
      text.length,
      "character_count is the streamed text length"
    );
    Assert.greaterOrEqual(Number(run.extra.time_to_first_chunk), 0);
    Assert.notEqual(run.extra.average_chunk_time, null);
    Assert.greaterOrEqual(Number(run.extra.average_chunk_time), 0);

    for await (const chunk of engine.runWithGenerator({
      ...RUN_REQUEST,
      nPredict: 4,
      minOutputBufferSize: 20,
    })) {
      void chunk;
    }
    await engine.terminate();

    const single = events("engineRun")[1].extra;
    Assert.equal(Number(single.token_count), 4);
    Assert.equal(
      single.average_chunk_time,
      null,
      "One chunk has no chunk cadence to report"
    );
  } finally {
    await cleanup();
  }
});

add_task(async function test_download_failure() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    let thrown;
    await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-download",
      featureId: "link-preview",
      modelId: "acme-not-found/bert",
      modelFile: "config.json",
    }).catch(e => (thrown = e));
    Assert.ok(thrown, "A missing model fails engine creation");

    Assert.equal(events("engineCreationFailure").length, 1);
    Assert.equal(
      lastError("engineCreationFailure"),
      thrown.name,
      "The creation failure records the download error"
    );
    Assert.equal(
      events("engineCreationFailure").at(-1).extra.host_process,
      "hwinference"
    );
    const steps = events("modelDownload").map(e => e.extra.step);
    Assert.ok(
      steps.includes("end_file_download_failed"),
      "The download steps say which file failed"
    );
    Assert.equal(
      steps.at(-1),
      "end_download_failed",
      "The download ends with a failure step"
    );
    Assert.equal(events("runInferenceFailure").length, 0);
  } finally {
    await cleanup();
  }
});

add_task(async function test_aborted_creation() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    const controller = new AbortController();
    controller.abort();
    await Assert.rejects(
      createEngine(
        {
          ...TINYSTORIES_ENGINE_OPTIONS,
          engineId: "hwi-tel-abort",
          featureId: "link-preview",
        },
        null,
        controller.signal
      ),
      error => error.name === "AbortError"
    );
    Assert.equal(events("engineCreationFailure").length, 1);
    Assert.equal(
      lastError("engineCreationFailure"),
      "AbortError",
      "The creation failure names the abort"
    );
  } finally {
    await cleanup();
  }
});

add_task(async function test_process_crash_mid_generation() {
  // Sanitizer builds have no crash reporter, so their signal handler turns
  // the deliberate crash into a sanitizer report that fails the run.
  if (AppConstants.ASAN || AppConstants.TSAN) {
    ok(true, "Skipping the deliberate crash on a sanitizer build");
    return;
  }
  SimpleTest.expectChildProcessCrash();
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-crash",
      featureId: "link-preview",
      modelFile: "crash-me.gguf",
    });
    const { pid } = await getInferenceProcessInfo("hwInference");
    const shutdown = TestUtils.topicObserved(
      "ipc:utility-shutdown",
      (subject, data) => parseInt(data, 10) === pid
    );

    await Assert.rejects(
      collectGeneratedText(
        engine.runWithGenerator({ ...RUN_REQUEST, nPredict: 64 })
      ),
      error => error.name === "AbortError"
    );
    await removeCrashDump(shutdown);
    noteIntentionalUtilityCrash(pid);

    Assert.equal(events("runInferenceFailure").length, 1);
    Assert.equal(
      lastError("runInferenceFailure"),
      "TextGenerator.generate: the inference process went away",
      "The run failure says the process died"
    );
    Assert.equal(
      events("runInferenceFailure").at(-1).extra.host_process,
      "hwinference"
    );
    Assert.equal(events("runInferenceSuccessFlow").length, 0);
    await engine.terminate();
  } finally {
    await cleanup();
  }
});

add_task(async function test_terminate_mid_generation() {
  const { cleanup } = await setup({ prefs: HWI_PREFS });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-tel-terminate",
      featureId: "link-preview",
      numContext: 4096,
    });
    await Assert.rejects(
      (async () => {
        for await (const chunk of engine.runWithGenerator({
          prompt: TINYSTORIES_STORYTELLER_PROMPT,
          samplers: TINYSTORIES_GREEDY_SAMPLERS,
          nPredict: 4096,
          minOutputBufferSize: 1,
          stopOnEndOfGenerationTokens: false,
        })) {
          void chunk;
          await engine.terminate();
        }
      })(),
      error => error.name === "AbortError"
    );

    Assert.equal(events("runInferenceFailure").length, 1);
    Assert.equal(
      lastError("runInferenceFailure"),
      "TextGenerator.generate: the generator was terminated",
      "The run failure says the caller tore the generator down"
    );
  } finally {
    await cleanup();
  }
});
