/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global TextGenerator */

const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);

const EXPECTED_MARKERS = [
  "MLProcessAcquire",
  "MLGeneratorCreate",
  "MLGeneratorRun",
  "MLChunkSend",
  "MLBackendInit",
  "MLModelPrefill",
  "MLModelDecode",
];

function getPayloads(profile, type) {
  return ProfilerTestUtils.getPayloadsOfTypeFromAllThreads(profile, type);
}

add_task(async function test_ml_pipeline_markers() {
  await ProfilerTestUtils.startProfiler({
    features: ["stackwalk", "js"],
    threads: ["GeckoMain", "TextGenerator", "llama.cpp"],
  });

  const generator = await createTinyStoriesGenerator();

  // Captured before the teardown: the process goes away with its last
  // generator, and its markers with it.
  let profile;
  try {
    await generator.generate(
      {
        messages: [{ role: "user", content: "Once upon a time" }],
        maxTokens: 16,
        bufferLength: 4,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
      },
      () => {}
    );
    profile = await ProfilerTestUtils.stopNowAndGetProfile();
  } finally {
    generator.terminate();
  }

  for (const type of EXPECTED_MARKERS) {
    Assert.greater(getPayloads(profile, type).length, 0, `${type} was emitted`);

    Assert.ok(
      ProfilerTestUtils.getSchema(profile, type),
      `${type} has a marker schema`
    );
  }

  const threadsFor = type => {
    const found = new Set();
    (function walk(proc) {
      for (const thread of proc.threads) {
        const { markers } = thread;
        for (const t of markers.data) {
          if (t[markers.schema.data]?.type === type) {
            found.add(thread.name);
          }
        }
      }
      proc.processes.forEach(walk);
    })(profile);
    return found;
  };
  Assert.ok(
    threadsFor("MLGeneratorRun").has("GeckoMain"),
    "The parent half of a generation is on GeckoMain"
  );
  Assert.ok(
    [...threadsFor("MLModelPrefill")].some(name =>
      name.startsWith("TextGenerator")
    ),
    "The model half is on the generator thread in HWInference"
  );

  const generate = getPayloads(profile, "MLGeneratorRun")[0];
  Assert.greater(generate.computeMs, 0, "The model compute is reported");
  Assert.greater(
    generate.prefillTokensPerSecond,
    0,
    "The prefill throughput is reported alongside the decode one, so the " +
      "label cannot imply the whole interval ran at decode speed"
  );
  Assert.greaterOrEqual(
    generate.overheadMs,
    0,
    "The overhead around the model is reported"
  );
  Assert.greaterOrEqual(
    generate.deliverMs,
    0,
    "What the caller's callback cost is reported"
  );
  Assert.greater(
    generate.chunkTokens,
    0,
    "The chunk size the ttfc number refers to is reported"
  );

  const prefill = getPayloads(profile, "MLModelPrefill")[0];
  Assert.greater(prefill.promptTokens, 0, "The prompt tokens are counted");
  const decode = getPayloads(profile, "MLModelDecode")[0];
  Assert.ok(
    decode,
    "The prompt phase boundary was observed, so a decode phase is reported"
  );

  Assert.equal(
    getPayloads(profile, "MLFailed").length,
    0,
    "A clean run reports no failed phase"
  );

  Assert.greater(
    getPayloads(profile, "MLChunkSend").length,
    0,
    "The child marked the chunks it streamed out"
  );
});

add_task(async function test_teardown_mid_generation_is_not_a_failure() {
  await ProfilerTestUtils.startProfiler({
    features: ["stackwalk", "js"],
    threads: ["GeckoMain", "TextGenerator"],
  });

  const generator = await createTinyStoriesGenerator();
  // Terminate only once a chunk arrived, so it lands mid-generation.
  let sawChunk;
  const firstChunk = new Promise(resolve => {
    sawChunk = resolve;
  });
  const generation = generator.generate(
    {
      messages: [{ role: "user", content: "Once upon a time" }],
      maxTokens: 4096,
      bufferLength: 1,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    },
    () => sawChunk()
  );
  await firstChunk;
  generator.terminate();
  await Assert.rejects(
    generation,
    /terminated/,
    "The abandoned generation rejects"
  );

  const profile = await ProfilerTestUtils.stopNowAndGetProfile();

  Assert.equal(
    getPayloads(profile, "MLFailed").length,
    0,
    "A teardown the caller asked for is not marked as a failed phase"
  );
  const stages = getPayloads(profile, "MLCancel").map(p => p.stage);
  Assert.ok(stages.includes("teardown"), "The teardown is marked as a cancel");
});

// The MLEngine path adds no markers of its own: what it contributes is the
// feature name on the generator markers.
add_task(async function test_engine_path_names_the_feature() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });

  await ProfilerTestUtils.startProfiler({
    features: ["stackwalk", "js"],
    threads: ["GeckoMain", "TextGenerator"],
  });

  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "marker-engine",
      featureId: "link-preview",
    });
    for await (const chunk of engine.runWithGenerator({
      prompt: [{ role: "user", content: "Once upon a time" }],
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
      nPredict: 16,
    })) {
      void chunk;
    }
    const profile = await ProfilerTestUtils.stopNowAndGetProfile();
    await engine.terminate();

    const create = getPayloads(profile, "MLGeneratorCreate")[0];
    Assert.ok(create, "The generator create was emitted");
    Assert.equal(
      create.featureId,
      "link-preview",
      "The create names the feature that asked for it"
    );
    const run = getPayloads(profile, "MLGeneratorRun")[0];
    Assert.ok(run, "The generator run was emitted");
    Assert.equal(run.featureId, "link-preview", "So does the run");
    Assert.greater(run.generatedTokens, 0, "The run generated tokens");
  } finally {
    await cleanup();
  }
});

add_task(async function test_abandoned_stream_is_marked_cancelled() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });

  await ProfilerTestUtils.startProfiler({
    features: ["stackwalk", "js"],
    threads: ["GeckoMain", "TextGenerator"],
  });

  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "cancelled-engine",
      featureId: "link-preview",
    });
    for await (const chunk of engine.runWithGenerator({
      prompt: [{ role: "user", content: "Once upon a time" }],
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
      nPredict: 8192,
      minOutputBufferSize: 1,
      stopOnEndOfGenerationTokens: false,
    })) {
      void chunk;
      break;
    }
    await engine.terminate();

    const profile = await ProfilerTestUtils.stopNowAndGetProfile();

    const stages = getPayloads(profile, "MLCancel").map(p => p.stage);
    Assert.ok(
      stages.includes("requested"),
      "Abandoning the loop asked the generator to stop"
    );
    const generatorRun = getPayloads(profile, "MLGeneratorRun")[0];
    Assert.ok(
      generatorRun,
      "The cancelled generation is still marked as a generation"
    );
    Assert.equal(
      generatorRun.reason,
      "cancelled",
      "A cancel is a finish reason on the run, not a run of its own"
    );
    Assert.greater(
      generatorRun.generatedTokens,
      0,
      "The cancelled generation reports the tokens it produced, so it " +
        "cannot read as a run that generated nothing"
    );
    Assert.ok(
      !stages.includes("teardown"),
      "Waiting for the cancelled run to settle means the terminate no " +
        "longer races its reply"
    );
  } finally {
    await cleanup();
  }
});
