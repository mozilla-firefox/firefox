/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

// Shared options and helpers for the structural-smoke tasks below.
// The greedy sampler chain must terminate in `dist`: top-k:1 alone is
// just a filter, and without `dist` to pick the surviving token
// llama.cpp crashes.
const LLAMA_SMOKE_OPTIONS = {
  backend: "llama.cpp",
  engineId: "ml-smoke-test-llama-smoke",
  taskName: "text-generation",
  modelId: "Mozilla/test-llama",
  modelFile: "TinyStories-656K.Q8_0.gguf",
  modelRevision: "main",
};

const LLAMA_SMOKE_GREEDY = [{ type: "top-k", topK: 1 }, { type: "dist" }];

const LLAMA_SMOKE_PROMPT_A = [
  { role: "system", content: "You are a friendly storyteller." },
  { role: "user", content: "Once upon a time there was a small mouse who" },
];

const LLAMA_SMOKE_PROMPT_B = [
  { role: "system", content: "You are a friendly storyteller." },
  { role: "user", content: "Deep in the forest, a tall green tree" },
];

async function runLlamaSmokeGen(
  engine,
  prompt,
  samplers = LLAMA_SMOKE_GREEDY,
  nPredict = 32
) {
  let text = "";
  const generator = engine.runWithGenerator({ prompt, samplers, nPredict });
  let result;
  do {
    result = await generator.next();
    if (!result.done) {
      text += result.value.text ?? "";
    }
  } while (!result.done);
  return text;
}

async function sha256Hex(str) {
  const bytes = new TextEncoder().encode(str);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)]
    .map(b => b.toString(16).padStart(2, "0"))
    .join("");
}

// Fraction of characters that are printable ASCII or common whitespace.
// A working tokenizer + decoder should be ~1.0; a busted one drops fast.
function printableRatio(text) {
  if (!text.length) {
    return 0;
  }
  let n = 0;
  for (const ch of text) {
    const code = ch.codePointAt(0);
    if (
      (code >= 0x20 && code <= 0x7e) ||
      code === 0x09 ||
      code === 0x0a ||
      code === 0x0d
    ) {
      n++;
    }
  }
  return n / [...text].length;
}

// Fraction of whitespace-split tokens that are distinct. Low values
// mean the decoder is looping ("the the the the ...").
function distinctTokenRatio(text) {
  const tokens = text.trim().split(/\s+/).filter(Boolean);
  if (!tokens.length) {
    return 0;
  }
  return new Set(tokens).size / tokens.length;
}

/**
 * Runs a full end-to-end test on the native ONNX backend
 */
add_task(async function test_ml_smoke_test_onnx() {
  const { cleanup } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine({
    taskName: "text-classification",
    modelId: "acme/bert",
    dtype: "q8",
    backend: "onnx-native",
    modelHubUrlTemplate: "{model}/resolve/{revision}",
  });
  const inferencePromise = engineInstance.run({ args: ["dummy data"] });

  const res = await inferencePromise;
  Assert.equal(res[0].label, "LABEL_0", "The text gets classified");

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

async function llama_crash() {
  const { cleanup } = await setup();

  SimpleTest.expectChildProcessCrash();

  try {
    const crashMan = Services.crashmanager;
    // A content process can emit ipc:content-shutdown for a normal teardown,
    // which carries no crash information. Wait for the abnormal one so that
    // the dumpID set by ContentParent is present on the subject.
    const contentShutdown = TestUtils.topicObserved(
      "ipc:content-shutdown",
      (subject, data) => {
        info(`ipc:content-shutdown: data=${data} subject=${subject}`);
        return subject instanceof Ci.nsIPropertyBag2 && subject.get("abnormal");
      }
    );

    const engine = await createEngine({
      modelId: "Mozilla/test-llama",
      taskName: "text-classification",
      modelFile: "crash-me.gguf",
      modelRevision: "main",
      backend: "llama.cpp",
      logLevel: "Debug",
    });
    const prompt = [
      { role: "system", content: "blah" },
      {
        role: "user",
        content: "This is a test that crashes",
      },
    ];
    info("Calling runWithGenerator");
    let sawCrash = false;
    try {
      for await (const val of engine.runWithGenerator({
        prompt,
      })) {
        info(val.text);
      }
    } catch (err) {
      sawCrash = true;
      info(`failed with error ${err.message}`);

      let [subject, data] = await contentShutdown;

      info(`ipc:content-shutdown: data=${data} subject=${subject}`);

      const dumpID = subject.get("dumpID");
      if (AppConstants.MOZ_CRASHREPORTER && dumpID === null) {
        // This test does not appear to generate minidumps, it is unclear why.
        // We should turn this into an `ok()` call once we fix the underlying
        // issue in bug 2003271.
        dump("There should be a dumpID");
      }

      if (AppConstants.MOZ_CRASHREPORTER && dumpID !== null) {
        await crashMan.ensureCrashIsPresent(dumpID);
        let minidumpDirectory = Services.dirsvc.get("ProfD", Ci.nsIFile);
        minidumpDirectory.append("minidumps");

        let dumpfile = minidumpDirectory.clone();
        dumpfile.append(dumpID + ".dmp");
        if (dumpfile.exists()) {
          info(`Removal of ${dumpfile.path}`);
          dumpfile.remove(false);
        }
        let extrafile = minidumpDirectory.clone();
        extrafile.append(dumpID + ".extra");
        info(`Removal of ${extrafile.path}`);
        if (extrafile.exists()) {
          extrafile.remove(false);
        }
        info(`cleaning up ${subject} ${data}`);
      }
    }
    Assert.ok(sawCrash, "the crash model must crash the serving process");
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
}

async function llama_works({
  prompt = [
    { role: "system", content: "blah" },
    {
      role: "user",
      content: "This is a test that works",
    },
  ],
  expectMultiChunkPrefill = false,
} = {}) {
  const { cleanup } = await setup();
  try {
    info("Create the engine for a normal run");
    const engine = await createEngine({
      taskName: "text-classification",
      modelId: "Mozilla/test-llama",
      modelFile: "TinyStories-656K.Q8_0.gguf",
      modelRevision: "main",
      backend: "llama.cpp",
      logLevel: "Debug",
    });

    const samplers = [
      {
        type: "top-k",
        topK: 3,
      },
      {
        type: "top-p",
        topP: 0.95,
      },

      {
        type: "logit-bias",
        logitBias: [{ token: 5, bias: -1000 }],
      },

      {
        type: "dist",
      },
    ];

    info("Calling runWithGenerator for normal run");
    const generator = engine.runWithGenerator({
      prompt,
      samplers,
    });
    let result;
    do {
      result = await generator.next();
      if (!result.done) {
        info(result.value.text);
      }
    } while (!result.done);

    info("Normal run worked");

    const { metrics } = result.value;
    Assert.ok(metrics, "metrics should be present on the run result");
    Assert.ok(
      Array.isArray(metrics.runTimestamps),
      "metrics.runTimestamps should be an array"
    );
    const timestampNames = metrics.runTimestamps.map(t => t.name);
    for (const name of [
      "initializationStart",
      "initializationEnd",
      "runStart",
      "runEnd",
    ]) {
      Assert.ok(
        timestampNames.includes(name),
        `metrics.runTimestamps should include ${name}`
      );
    }
    Assert.greater(metrics.inputTokens, 0, "inputTokens should be > 0");
    Assert.greater(metrics.outputTokens, 0, "outputTokens should be > 0");
    Assert.greaterOrEqual(
      metrics.inferenceTime,
      0,
      "inferenceTime should be >= 0"
    );
    Assert.greaterOrEqual(
      metrics.decodingTime,
      0,
      "decodingTime should be >= 0"
    );
    Assert.greaterOrEqual(
      metrics.timeToFirstToken,
      0,
      "timeToFirstToken should be >= 0"
    );

    if (expectMultiChunkPrefill) {
      // Default minOutputBufferSize is 20: a prompt above that exercises
      // the multi-chunk prefill path where the runner flushes prompt chunks
      // before isPhaseCompleted=true.
      Assert.greater(
        metrics.inputTokens,
        20,
        "inputTokens should exceed the default minOutputBufferSize"
      );
    }
  } finally {
    info("Destroy the engine");
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
}

async function llama_fails_with_wrong_samplers() {
  await EngineProcess.destroyMLEngine();
  await IndexedDBCache.init({ reset: true });

  const { cleanup } = await setup();
  try {
    info("Create the engine for a normal run");
    const engine = await createEngine({
      taskName: "text-classification",
      modelId: "Mozilla/test-llama",
      modelFile: "TinyStories-656K.Q8_0.gguf",
      modelRevision: "main",
      backend: "llama.cpp",
      logLevel: "Debug",
    });

    const prompt = [
      { role: "system", content: "blah" },
      {
        role: "user",
        content: "This is a test that works",
      },
    ];

    const samplers = [
      {
        type: "top-k",
        topK: 3,
      },
      {
        type: "top-p",
        topP: 0.95,
      },

      {
        type: "logit-bias",
        logitBias: [{ token: 5, bias: -1000 }],
      },

      {
        type: "dist-invalid",
      },
    ];

    info("Calling runWithGenerator for normal run with expected failure");
    const runEngine = async () => {
      await engine.run({ prompt, samplers });
    };

    await Assert.rejects(
      runEngine(),
      err =>
        String(err?.message ?? err).includes(
          "LlamaRunner.createGenerationStream: 'dist-invalid'"
        ),
      "The call should be rejected because it used an invalid sampler"
    );
  } finally {
    info("Destroy the engine");
    await EngineProcess.destroyMLEngine();
    await IndexedDBCache.init({ reset: true });
    await cleanup();
  }
}

/**
 * Runs a full end-to-end test on the llama.cpp backend with samplers and expected failure.
 */
add_task(async function test_ml_smoke_test_llama_fails() {
  await llama_fails_with_wrong_samplers();
});

add_task(async function test_ml_smoke_test_llama_sequential_runs() {
  await runOnBothInferenceProcesses(async () => {
    const { cleanup } = await setup();
    try {
      const engine = await createEngine({
        taskName: "text-generation",
        modelId: "Mozilla/test-llama",
        modelFile: "TinyStories-656K.Q8_0.gguf",
        modelRevision: "main",
        backend: "llama.cpp",
      });

      const request = {
        prompt: [
          { role: "system", content: "blah" },
          { role: "user", content: "Once upon a time there was" },
        ],
        nPredict: 16,
      };

      await engine.run(request);
      await engine.run(request);
      Assert.ok(true, "Two sequential run() calls completed without rejection");
    } finally {
      await EngineProcess.destroyMLEngine();
      await cleanup();
    }
  });
});

add_task(async function test_ml_smoke_test_llama_overlap_guard() {
  await runOnBothInferenceProcesses(async () => {
    const { cleanup } = await setup();
    try {
      const engine = await createEngine({
        taskName: "text-generation",
        modelId: "Mozilla/test-llama",
        modelFile: "TinyStories-656K.Q8_0.gguf",
        modelRevision: "main",
        backend: "llama.cpp",
      });

      const request = {
        prompt: [
          { role: "system", content: "blah" },
          { role: "user", content: "Once upon a time there was" },
        ],
        nPredict: 128,
      };

      const results = await Promise.allSettled([
        engine.run(request),
        engine.run(request),
      ]);

      const rejections = results
        .filter(r => r.status === "rejected")
        .map(r => String(r.reason?.message ?? r.reason));

      Assert.ok(
        rejections.some(m => m.includes("A generation is already in progress")),
        `Expected a rejection from the LlamaRunner guard, got: ${JSON.stringify(rejections)}`
      );
    } finally {
      await EngineProcess.destroyMLEngine();
      await cleanup();
    }
  });
});

// Abandoning a generator has to end the underlying run, otherwise the
// LlamaRunner concurrency guard rejects the next request.
add_task(
  async function test_ml_smoke_test_llama_abandoned_generator_allows_next() {
    await runOnBothInferenceProcesses(async () => {
      const { cleanup } = await setup();
      try {
        const engine = await createEngine({
          taskName: "text-generation",
          modelId: "Mozilla/test-llama",
          modelFile: "TinyStories-656K.Q8_0.gguf",
          modelRevision: "main",
          backend: "llama.cpp",
        });

        const request = {
          prompt: [
            { role: "system", content: "blah" },
            { role: "user", content: "Once upon a time there was" },
          ],
          nPredict: 200,
        };

        let chunks = 0;
        for await (const chunk of engine.runWithGenerator(request)) {
          Assert.ok(chunk, "Received a chunk before abandoning the generator");
          chunks++;
          break;
        }
        Assert.equal(chunks, 1, "Abandoned the generator after one chunk");

        // Both forms hit the same LlamaRunner guard.
        const result = await engine.run(request);
        Assert.ok(
          result.finalOutput.length,
          "The request after an abandoned generator generated text"
        );
      } finally {
        await EngineProcess.destroyMLEngine();
        await cleanup();
      }
    });
  }
);

// An abandoned run is still recorded as run_inference_success_flow, so
// engine_run has to report it too.
add_task(
  async function test_ml_smoke_test_llama_abandoned_records_engine_run() {
    await runOnBothInferenceProcesses(async () => {
      const { cleanup } = await setup();
      try {
        const engine = await createEngine({
          taskName: "text-generation",
          modelId: "Mozilla/test-llama",
          modelFile: "TinyStories-656K.Q8_0.gguf",
          modelRevision: "main",
          backend: "llama.cpp",
        });

        const request = {
          prompt: [
            { role: "system", content: "blah" },
            { role: "user", content: "Once upon a time there was" },
          ],
          nPredict: 200,
        };

        for await (const chunk of engine.runWithGenerator(request)) {
          Assert.ok(chunk, "Received a chunk before abandoning the generator");
          break;
        }

        await waitForCondition(
          () =>
            Glean.firefoxAiRuntime.runInferenceSuccessFlow.testGetValue()
              ?.length,
          "Waiting for the abandoned run to complete in the child."
        );

        await waitForCondition(
          () => Glean.firefoxAiRuntime.engineRun.testGetValue()?.length,
          "Waiting for engine_run to be recorded for the abandoned run."
        );

        const [engineRun] = Glean.firefoxAiRuntime.engineRun.testGetValue();
        Assert.equal(
          engineRun.extra.backend,
          "llama.cpp",
          "The recorded run reports the backend that generated it"
        );
      } finally {
        await EngineProcess.destroyMLEngine();
        await cleanup();
      }
    });
  }
);

/**
 * Runs a full end-to-end test on the llama.cpp backend with a model that loads in llama but crashes during inference.
 */
add_task(async function test_ml_smoke_test_llama_crash() {
  info("Doing a crash call");
  await llama_crash();
  info(
    "Doing a normal call after the crash to verify it's up and running again"
  );
  await llama_works();
});

/**
 * Verifies metrics are correct when the prompt exceeds the runner's
 * default minOutputBufferSize (20), forcing the prefill phase to be
 * split across multiple chunks before isPhaseCompleted is set.
 */
add_task(async function test_ml_smoke_test_llama_long_prompt_metrics() {
  await runOnBothInferenceProcesses(async () => {
    await llama_works({
      prompt: [
        { role: "system", content: "You are a friendly storyteller." },
        {
          role: "user",
          content:
            "Tell me a short story about a brave little mouse who travels " +
            "across a great forest, meets many friends along the way, and " +
            "finally finds a tiny treasure chest hidden behind a waterfall " +
            "at the top of the tallest hill in the whole valley.",
        },
      ],
      expectMultiChunkPrefill: true,
    });
  });
});

// Structural smoke for the native llama.cpp backend with greedy
// decoding. The tests above cover engine-API correctness; the tasks
// below cover output-content invariants: the generation is printable
// and diverse, different prompts produce different outputs, and the
// greedy output matches a pinned golden text + SHA-256 hash.

add_task(async function test_ml_smoke_test_llama_output_looks_like_text() {
  await runOnBothInferenceProcesses(async () => {
    const { cleanup } = await setup();
    try {
      const engine = await createEngine(LLAMA_SMOKE_OPTIONS);
      try {
        const text = await runLlamaSmokeGen(engine, LLAMA_SMOKE_PROMPT_A);
        info(`Output: ${text}`);

        Assert.greater(text.length, 0, "Generation produced text");
        Assert.notEqual(
          text.trim(),
          LLAMA_SMOKE_PROMPT_A[1].content.trim(),
          "Output is not a verbatim echo of the user prompt"
        );

        const pr = printableRatio(text);
        info(`Printable-ASCII ratio: ${pr.toFixed(3)}`);
        Assert.greater(
          pr,
          0.9,
          `Output should be mostly printable text (got ${pr.toFixed(3)})`
        );

        const dr = distinctTokenRatio(text);
        info(`Distinct-token ratio: ${dr.toFixed(3)}`);
        Assert.greater(
          dr,
          0.3,
          `Output should not be a degenerate loop (got ${dr.toFixed(3)})`
        );
      } finally {
        await engine.terminate?.();
      }
    } finally {
      await EngineProcess.destroyMLEngine();
      await cleanup();
    }
  });
});

add_task(async function test_ml_smoke_test_llama_prompt_sensitive() {
  await runOnBothInferenceProcesses(async () => {
    const { cleanup } = await setup();
    try {
      const engine = await createEngine(LLAMA_SMOKE_OPTIONS);
      try {
        const a = await runLlamaSmokeGen(engine, LLAMA_SMOKE_PROMPT_A);
        const b = await runLlamaSmokeGen(engine, LLAMA_SMOKE_PROMPT_B);
        info(`Prompt A output: ${a}`);
        info(`Prompt B output: ${b}`);
        Assert.notEqual(
          a,
          b,
          "Different prompts should produce different greedy outputs"
        );
      } finally {
        await engine.terminate?.();
      }
    } finally {
      await EngineProcess.destroyMLEngine();
      await cleanup();
    }
  });
});

// Golden-output check. Catches silent changes in token selection
// (quantization drift, attention bug, upstream llama.cpp roll).
//
// Greedy outputs diverge by CPU architecture: different SIMD vector
// widths in llama.cpp's matmul/attention kernels (NEON on aarch64, AVX
// on x86-64, SSE on x86) shift logits enough to flip argmax on a
// handful of tokens, so the constants are keyed on the architecture
// Services.sysinfo reports. macOS Intel 10.15 exhibits within-engine
// non-determinism for TinyStories, so the golden assertion runtime-skips
// there. Re-pin both text and hash from a try push when a llama.cpp roll
// legitimately changes outputs.
const LLAMA_SMOKE_GOLDEN = {
  aarch64: {
    text: "Suddenly, the mouse stopped! \nThe little mouse was scared, but he was scared. He had been so brave. He was scared, but he was still wearing his ",
    hash: "a59803f3d1c9d983f14c54fadbe9de3b73a1053780f01bc7768fa2bc498f6005",
  },
  "x86-64": {
    text: "Suddenly, the mouse stopped! \nThe old lady was surprised to see a beautiful song. The old lady was so surprised. She had never seen a little mouse and ",
    hash: "67b4cf2e37139e20795938ab3dedbdd040ffc2143e2ded210b0838de37581fa9",
  },
  x86: {
    text: "Suddenly, the mouse stopped! \nThe old lady was surprised to see a beautiful song. The old lady was so surprised. She had never seen anything else ",
    hash: "afc7fdbad9f3b10f7e68a23c3ef34f7118a23a17d3b8d5185636de3c8c8e2989",
  },
};

add_task(async function test_ml_smoke_test_llama_golden_text() {
  await runOnBothInferenceProcesses(async () => {
    const arch = Services.sysinfo.getProperty("arch");
    const isMacIntel = AppConstants.platform === "macosx" && arch !== "aarch64";
    if (isMacIntel) {
      ok(
        true,
        "Skipping golden-text on macOS Intel 10.15: TinyStories greedy " +
          "output is in a third arch bucket and within-engine determinism " +
          "is unreliable there (Bug 2047025)."
      );
      return;
    }

    const golden = LLAMA_SMOKE_GOLDEN[arch];
    if (!golden) {
      ok(true, `Skipping golden-text: no output pinned for arch ${arch}.`);
      return;
    }

    const { cleanup } = await setup();
    try {
      const engine = await createEngine(LLAMA_SMOKE_OPTIONS);
      try {
        const text = await runLlamaSmokeGen(engine, LLAMA_SMOKE_PROMPT_A);
        const hash = await sha256Hex(text);
        info(`Greedy text: ${text}`);
        info(`Greedy SHA-256: ${hash}`);
        Assert.equal(
          text,
          golden.text,
          "Greedy output matches the pinned golden text"
        );
        Assert.equal(
          hash,
          golden.hash,
          "Greedy output hash matches the pinned golden hash"
        );
      } finally {
        await engine.terminate?.();
      }
    } finally {
      await EngineProcess.destroyMLEngine();
      await cleanup();
    }
  });
});
