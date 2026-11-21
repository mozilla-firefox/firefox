/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

// Structural smoke for the native llama.cpp backend against the real
// Link Preview model (SmolLM2-360M Q8). Runs under the perftest
// harness so hooks_local_hub.py can serve the GGUF from
// MOZ_FETCHES_DIR/onnx-models/; the mochitest sibling in
// browser_ml_native.js covers the in-tree TinyStories model.

const {
  SMOKE_PROMPT,
  SMOKE_SAMPLERS,
  SMOKE_N_PREDICT,
  SMOKE_EXPECTED_TEXT,
  SMOKE_EXPECTED_HASH,
  runSmokeTest,
  sha256Hex,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs"
);

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_ml_llama_smollm2_smoke.js",
  description:
    "Structural smoke for the native llama.cpp backend with SmolLM2 360M Instruct.",
  options: {
    default: {
      perfherder: false,
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

requestLongerTimeout(20);

const SMOLLM2_OPTIONS = {
  backend: "llama.cpp",
  engineId: "link-preview-smoke-smollm2",
  featureId: "link-preview",
  taskName: "llama-text-generation",
  modelId: "HuggingFaceTB/SmolLM2-360M-Instruct-GGUF",
  modelFile: "smollm2-360m-instruct-q8_0.gguf",
  modelRevision: "main",
  modelHubUrlTemplate: "{model}/{revision}",
  useMmap: true,
  useMlock: false,
};

const PROMPT = [
  { role: "system", content: "You are a friendly storyteller." },
  { role: "user", content: "Deep in the forest, a tall green tree" },
];

// Helpers mirror the ones in browser_ml_native.js.

// Avoid `??` and `?.` here — mozperftest parses tests with esprima
// (Python port) which doesn't recognise nullish-coalescing or optional
// chaining. browser_ml_llama_summarizer_perf.js follows the same rule.
async function runGen(
  engine,
  prompt,
  samplers = SMOKE_SAMPLERS,
  nPredict = SMOKE_N_PREDICT
) {
  let text = "";
  let metrics;
  const generator = engine.runWithGenerator({ prompt, samplers, nPredict });
  let result;
  do {
    result = await generator.next();
    if (!result.done) {
      text += result.value.text || "";
    } else if (result.value) {
      metrics = result.value.metrics;
    }
  } while (!result.done);
  return { text, metrics };
}

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

function distinctTokenRatio(text) {
  const tokens = text.trim().split(/\s+/).filter(Boolean);
  if (!tokens.length) {
    return 0;
  }
  return new Set(tokens).size / tokens.length;
}

add_task(async function test_smollm2_survives_and_metrics_populated() {
  const process = usesHWInferenceProcess() ? "HWInference" : "inference";
  info(`test_smollm2_survives_and_metrics_populated: starting in ${process}`);
  const { cleanup, engine } = await initializeEngine(SMOLLM2_OPTIONS);
  info("test_smollm2_survives_and_metrics_populated: engine ready");
  try {
    const { text, metrics } = await runGen(engine, SMOKE_PROMPT);
    info(`Output: ${text}`);
    Assert.greater(text.length, 0, "SmolLM2 produced text");
    Assert.ok(metrics, "metrics populated");
    Assert.greater(metrics.inputTokens, 0, "inputTokens > 0");
    Assert.greater(metrics.outputTokens, 0, "outputTokens > 0");
    Assert.greaterOrEqual(metrics.decodingTime, 0, "decodingTime defined");
    Assert.greaterOrEqual(
      metrics.timeToFirstToken,
      0,
      "timeToFirstToken defined"
    );

    // mozperftest requires at least one perfMetrics emission per run,
    // even with perfherder reporting disabled.
    const reported = [
      {
        name: `smollm2-inputTokens${hwInferenceSuffix()}`,
        values: [metrics.inputTokens],
        value: metrics.inputTokens,
      },
      {
        name: `smollm2-outputTokens${hwInferenceSuffix()}`,
        values: [metrics.outputTokens],
        value: metrics.outputTokens,
      },
      {
        name: `smollm2-tokensPerSecond${hwInferenceSuffix()}`,
        values: [metrics.tokensPerSecond],
        value: metrics.tokensPerSecond,
      },
    ];
    info(`perfMetrics | ${JSON.stringify(reported)}`);
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

add_task(async function test_smollm2_output_looks_like_text() {
  info("test_smollm2_output_looks_like_text: starting");
  const { cleanup, engine } = await initializeEngine(SMOLLM2_OPTIONS);
  info("test_smollm2_output_looks_like_text: engine ready");
  try {
    const { text } = await runGen(engine, SMOKE_PROMPT);
    info(`Output: ${text}`);
    Assert.notEqual(
      text.trim(),
      SMOKE_PROMPT[1].content.trim(),
      "Output is not a verbatim echo of the user prompt"
    );
    const pr = printableRatio(text);
    info(`Printable-ASCII ratio: ${pr.toFixed(3)}`);
    Assert.greater(
      pr,
      0.9,
      `Output should be mostly printable (got ${pr.toFixed(3)})`
    );
    const dr = distinctTokenRatio(text);
    info(`Distinct-token ratio: ${dr.toFixed(3)}`);
    Assert.greater(
      dr,
      0.3,
      `Output should not be a degenerate loop (got ${dr.toFixed(3)})`
    );
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

add_task(async function test_smollm2_greedy_is_deterministic() {
  info("test_smollm2_greedy_is_deterministic: starting");
  const { cleanup, engine } = await initializeEngine(SMOLLM2_OPTIONS);
  info("test_smollm2_greedy_is_deterministic: engine ready");
  try {
    const { text: a } = await runGen(engine, SMOKE_PROMPT);
    const { text: b } = await runGen(engine, SMOKE_PROMPT);
    info(`Greedy A: ${a}`);
    info(`Greedy B: ${b}`);
    Assert.equal(a, b, "Two greedy runs of the same prompt produce same text");
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

add_task(async function test_smollm2_engine_is_prompt_sensitive() {
  info("test_smollm2_engine_is_prompt_sensitive: starting");
  const { cleanup, engine } = await initializeEngine(SMOLLM2_OPTIONS);
  info("test_smollm2_engine_is_prompt_sensitive: engine ready");
  try {
    const { text: a } = await runGen(engine, SMOKE_PROMPT);
    const { text: b } = await runGen(engine, PROMPT);
    info(`Prompt A output: ${a}`);
    info(`Prompt B output: ${b}`);
    Assert.notEqual(
      a,
      b,
      "Different prompts should produce different greedy outputs"
    );
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

add_task(async function test_smollm2_golden_text() {
  info("test_smollm2_golden_text: starting");
  const { cleanup, engine } = await initializeEngine(SMOLLM2_OPTIONS);
  info("test_smollm2_golden_text: engine ready");
  try {
    const { text } = await runGen(engine, SMOKE_PROMPT);
    const hash = await sha256Hex(text);
    info(`SmolLM2 greedy text: ${text}`);
    info(`SmolLM2 greedy SHA-256: ${hash}`);
    Assert.equal(
      text,
      SMOKE_EXPECTED_TEXT,
      "SmolLM2 greedy output matches the pinned golden text"
    );
    Assert.equal(
      hash,
      SMOKE_EXPECTED_HASH,
      "SmolLM2 greedy output hash matches the pinned golden hash"
    );
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});

// Drives the exact runSmokeTest function the shipped Link Preview code
// calls, against a real SmolLM2 engine, and asserts the Glean
// smoke_test event was recorded with the values downstream analysis
// depends on.
add_task(async function test_link_preview_run_smoke_test_records_telemetry() {
  info("test_link_preview_run_smoke_test_records_telemetry: starting");
  const LAST_BUILD_ID_PREF = "browser.ml.linkPreview.smokeTest.lastBuildID";
  Services.prefs.clearUserPref(LAST_BUILD_ID_PREF);
  Services.fog.testResetFOG();

  const { cleanup, engine } = await initializeEngine(SMOLLM2_OPTIONS);
  info("test_link_preview_run_smoke_test_records_telemetry: engine ready");
  try {
    await runSmokeTest({ engine, buildID: "test-build-id" });

    const events = Glean.genaiLinkpreview.smokeTest.testGetValue();
    Assert.equal(events && events.length, 1, "smoke_test event recorded once");
    const { extra } = events[0];
    Assert.equal(extra.matches_pinned, "true", "matches_pinned is true");
    Assert.equal(
      extra.output_hash,
      SMOKE_EXPECTED_HASH,
      "output_hash equals the pinned reference"
    );
    Assert.equal(
      extra.model_id,
      "HuggingFaceTB/SmolLM2-360M-Instruct-GGUF",
      "model_id is the pinned SmolLM2 identifier"
    );
    Assert.equal(extra.model_revision, "main", "model_revision is 'main'");
    Assert.equal(
      Services.prefs.getStringPref(LAST_BUILD_ID_PREF, ""),
      "test-build-id",
      "lastBuildID pref is updated after a successful run"
    );

    // Verify the sibling engine_run event carries the join-key extras
    // that downstream analysis depends on. Guards against silent
    // extra-key drift between metrics.yaml and the .record() call site.
    const engineRuns = Glean.firefoxAiRuntime.engineRun.testGetValue();
    Assert.ok(
      engineRuns && engineRuns.length,
      "engine_run event recorded for the smoke inference"
    );
    const sibling = engineRuns.find(e => e.extra.flow_id === extra.flow_id);
    Assert.ok(sibling, "an engine_run event shares the smoke_test flow_id");
    Assert.equal(
      sibling.extra.backend,
      "llama.cpp",
      "engine_run backend is llama.cpp"
    );
    Assert.equal(
      sibling.extra.backend_source_revision,
      "74ade52741203e5c8f81eaf06a96cb1cfe15f2a3",
      "engine_run.backend_source_revision equals the pinned LLAMA_CPP_VERSION"
    );

    // Second call with the same buildID must be a no-op.
    await runSmokeTest({ engine, buildID: "test-build-id" });
    const eventsAfter = Glean.genaiLinkpreview.smokeTest.testGetValue();
    Assert.equal(
      eventsAfter ? eventsAfter.length : 0,
      1,
      "second run with same buildID does not record another event"
    );
  } finally {
    Services.prefs.clearUserPref(LAST_BUILD_ID_PREF);
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});
