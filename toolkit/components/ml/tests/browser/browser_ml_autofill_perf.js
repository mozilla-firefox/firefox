/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Latency test for the ML Autofill model.
//
// Two field classifiers are exercised:
//   1. LEGACY: the single-model `text-classification` classifier
//      (Mozilla/tinybert-address-autofill) -- `test_ml_generic_pipeline`.
//   2. NEW ("Approach 3"): a triple-encoder deployed as TWO engines
//      (see toolkit/components/formautofill/shared/FormAutofillML.sys.mjs) --
//      `test_ml_autofill_two_engine_pipeline`:
//        - ENCODER: a stock `feature-extraction` model that turns each field's
//          token string into one mean-pooled embedding (run once per form over
//          the set of unique field sections).
//        - HEAD: a tiny custom `moz-formfill-head` pipeline that scores the
//          windowed features `[e_cur, e_prev, e_next, e_cur-e_prev, e_cur-e_next]`
//          (5 x 384 = 1920 dims) into a field type, once for all fields.

const EMBEDDING_DIM = 384;
const HEAD_FEATURE_DIM = 5 * EMBEDDING_DIM; // cur, prev, next, cur-prev, cur-next

// A shared representative form: each field's own tokens (id/name/placeholder/
// label words). BOTH classifiers are measured on THIS SAME form so the latency
// is an apples-to-apples per-form comparison. Values only need to be realistic
// in length/count; they don't affect latency materially.
const FIELD_TOKENS = [
  "firstname first name enter your first name",
  "lastname last name surname family name",
  "email email address your email",
  "tel phone phone number mobile",
  "address address line 1 street address",
  "address2 address line 2 apartment suite unit",
  "city town city suburb",
  "state province region county",
  "zip postal code postcode zip code",
  "country country region",
  "organization company organization",
];
const NUM_FIELDS = FIELD_TOKENS.length;

// NEW model (encoder): the unique section strings the encoder embeds once per
// form -- each field's own tokens plus the empty boundary section. Neighbor
// sections are other fields' own tokens, already present in this set.
const ENCODER_INPUTS = [...FIELD_TOKENS, ""];

// Prefix each token in `tokens` with `prefix` (FormAutofillHeuristics prefixes a
// field's previous-neighbor tokens with "bb" and next-neighbor tokens with "aa").
function prefixTokens(tokens, prefix) {
  return tokens
    .split(/\s+/)
    .filter(Boolean)
    .map(w => prefix + w)
    .join(" ");
}

// LEGACY model input: one combined string PER FIELD -- own tokens + the previous
// field's tokens ("bb"-prefixed) + the next field's tokens ("aa"-prefixed) --
// exactly the `mlData` the single text-classification model receives in
// production. The whole form is classified in ONE batched engine.run, matching
// the old FormAutofillML.detectFields (`args: [inputData]`).
const LEGACY_INPUTS = FIELD_TOKENS.map((own, i) => {
  const parts = [own];
  if (i > 0) {
    parts.push(prefixTokens(FIELD_TOKENS[i - 1], "bb"));
  }
  if (i < FIELD_TOKENS.length - 1) {
    parts.push(prefixTokens(FIELD_TOKENS[i + 1], "aa"));
  }
  return parts.join(" ");
});

// Synthetic head input: NUM_FIELDS rows of HEAD_FEATURE_DIM floats. The values
// don't affect latency, so they're small and deterministic for reproducibility.
function buildHeadRows(nRows, dim) {
  const rows = [];
  for (let i = 0; i < nRows; i++) {
    const row = new Array(dim);
    for (let j = 0; j < dim; j++) {
      row[j] = ((i * 31 + j) % 97) / 97 - 0.5;
    }
    rows.push(row);
  }
  return rows;
}

const ENGINES = {
  "autofill-encoder": {
    engineId: "autofill-encoder",
    metricPrefix: "AUTOFILL-encoder",
    taskName: "feature-extraction",
    modelId: "mozilla/form-autofill-embed",
    modelRevision: "v0.1.0",
    modelHubUrlTemplate: "{model}/{revision}",
    dtype: "q8",
    // Prefer the native onnxruntime when the platform bundles it, else wasm.
    backend: "best-onnx",
    numThreads: 2,
    request: {
      args: [ENCODER_INPUTS],
      // Raw attention-masked mean pooling, NO L2 normalization -- must match
      // how the head was trained (FormAutofillML.sys.mjs).
      options: { pooling: "mean", normalize: false },
    },
  },
  "autofill-head": {
    engineId: "autofill-head",
    metricPrefix: "AUTOFILL-head",
    taskName: "moz-formfill-head",
    modelId: "mozilla/form-autofill-head",
    modelRevision: "v0.1.0",
    modelHubUrlTemplate: "{model}/{revision}",
    dtype: "fp32",
    // Prefer the native onnxruntime when the platform bundles it, else wasm.
    backend: "best-onnx",
    numThreads: 2,
    request: {
      args: [buildHeadRows(NUM_FIELDS, HEAD_FEATURE_DIM)],
    },
  },
};

// "two-engine" keeps these apart from the runMLPerfTest("autofill") series
// in perfherder.
const e2eRunLatencyMetric = tag => `AUTOFILL-two-engine-e2e-run-latency-${tag}`;
const concurrentInitLatencyMetric = tag =>
  `AUTOFILL-two-engine-concurrent-init-latency-${tag}`;
const twoEngineMemoryMetric = tag =>
  `AUTOFILL-two-engine-total-memory-usage-${tag}`;

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_ml_autofill_perf.js",
  description:
    "Latency for the ML Autofill model (legacy single-model and new two-engine)",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        // Legacy single text-classification model (test_ml_generic_pipeline).
        {
          name: "AUTOFILL-pipeline-ready-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-initialization-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-model-run-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-total-memory-usage",
          unit: "MiB",
          shouldAlert: false,
        },
        {
          name: "tokenSpeed",
          unit: "tokens/s",
          shouldAlert: false,
          lowerIsBetter: false,
        },
        {
          name: "charactersSpeed",
          unit: "chars/s",
          shouldAlert: false,
          lowerIsBetter: false,
        },
        // New two-engine (encoder + head) architecture
        // (test_ml_autofill_two_engine_pipeline). Uppercase "AUTOFILL-" prefix
        // to match the legacy metrics above.
        // NOTE: these must be string LITERALS -- the mozperftest static parser
        // (mozperftest/script.py) reads this object and rejects identifiers /
        // expressions. Keep them in sync with the *_METRIC constants and the
        // engine metricPrefix values used below.
        {
          name: "AUTOFILL-two-engine-concurrent-init-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-two-engine-e2e-run-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-two-engine-total-memory-usage",
          unit: "MiB",
          shouldAlert: false,
        },
        // Per-engine latencies (encoder + head). The runtime appends the
        // backend tag; these match as substrings.
        {
          name: "AUTOFILL-encoder-pipeline-ready-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-encoder-initialization-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-encoder-model-run-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-head-pipeline-ready-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-head-initialization-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "AUTOFILL-head-model-run-latency",
          unit: "ms",
          shouldAlert: false,
        },
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

requestLongerTimeout(10);

/**
 * LEGACY: single-model text-classification autofill classifier.
 *
 * Classifies the WHOLE form in one batched run (one string per field, each
 * carrying its own tokens plus the aa/bb neighbor context), exactly as the old
 * FormAutofillML.detectFields did -- so `AUTOFILL-model-run-latency` is a
 * per-form cost directly comparable to the new pipeline's
 * `AUTOFILL-two-engine-e2e-run-latency`.
 */
add_task(async function test_ml_generic_pipeline() {
  const options = new PipelineOptions({
    taskName: "text-classification",
    modelId: "Mozilla/tinybert-address-autofill",
    modelHubUrlTemplate: "{model}/{revision}",
    modelRevision: "main",
    dtype: "int8",
    // Prefer the native onnxruntime when the platform bundles it, else wasm.
    backend: "best-onnx",
    numThreads: 2,
    timeoutMS: -1,
  });

  const request = {
    args: [LEGACY_INPUTS],
    options: { pooling: "mean", normalize: true },
  };

  await runMLPerfTest({ name: "autofill", options, request });
});

/**
 * Runs inference on an initialized engine `iterations` times and collects the
 * latency metrics, prefixed with the engine's `metricPrefix` (e.g.
 * "AUTOFILL-encoder").
 */
async function runEngineWithMetrics(engine, engineConfig, iterations, tag) {
  const journal = {};
  for (let i = 0; i < iterations; i++) {
    const res = await engine.run(engineConfig.request);
    const metrics = fetchMetrics(res.metrics);
    for (const [metricName, metricVal] of Object.entries(metrics)) {
      const key = `${engineConfig.metricPrefix}-${metricName}-${tag}`;
      (journal[key] = journal[key] || []).push(metricVal);
    }
  }
  return journal;
}

/**
 * NEW: two-engine (encoder + head) ML Autofill pipeline -- per-engine latency
 * plus the end-to-end per-form cost (encode -> head).
 */
add_task(async function test_ml_autofill_two_engine_pipeline() {
  await runMLPerfTestForEachBackend({
    name: "AUTOFILL-TWO-ENGINE",
    run: runTwoEnginePipeline,
  });
});

async function runTwoEnginePipeline({ backend, tag }) {
  const configs = Object.values(ENGINES);

  // Initialize both engines CONCURRENTLY, matching FormAutofillML.#ensureEngines
  // which fires both createEngine calls then `await Promise.all([...])`.
  const initBoth = () =>
    Promise.all(
      configs.map(async cfg => {
        const { cleanup, engine } = await initializeEngine(
          new PipelineOptions({ timeoutMS: -1, ...cfg, backend })
        );
        return { cleanup, engine, cfg };
      })
    );

  const combined = {};
  const merge = j => {
    for (const [k, v] of Object.entries(j)) {
      (combined[k] = combined[k] || []).push(...v);
    }
  };

  // Concurrent init wall-clock (Promise.all over both engines): the real
  // one-time first-use startup cost. Measured across ITERATIONS for a median,
  // tearing both engines down between runs so each is a fresh init.
  combined[concurrentInitLatencyMetric(tag)] = [];
  for (let i = 0; i < ITERATIONS; i++) {
    const t0 = performance.now();
    const insts = await initBoth();
    combined[concurrentInitLatencyMetric(tag)].push(performance.now() - t0);
    await EngineProcess.destroyMLEngine();
    for (const { cleanup } of insts) {
      await cleanup();
    }
  }

  // Fresh init for the per-engine run + end-to-end measurements.
  const instances = await initBoth();
  info("Encoder and head engines initialized");

  try {
    // Per-engine latency (encoder, then head).
    for (const { engine, cfg } of instances) {
      merge(await runEngineWithMetrics(engine, cfg, ITERATIONS, tag));
    }

    // End-to-end per-form latency: encode once, then score once -- the real flow.
    const encoder = instances[0];
    const head = instances[1];
    for (let i = 0; i < ITERATIONS; i++) {
      const start = performance.now();
      await encoder.engine.run(encoder.cfg.request);
      await head.engine.run(head.cfg.request);
      (combined[e2eRunLatencyMetric(tag)] =
        combined[e2eRunLatencyMetric(tag)] || []).push(
        performance.now() - start
      );
    }

    const memUsage = await getTotalMemoryUsage();
    (combined[twoEngineMemoryMetric(tag)] =
      combined[twoEngineMemoryMetric(tag)] || []).push(memUsage);
  } finally {
    // The next backend in the matrix starts from a clean engine process.
    await EngineProcess.destroyMLEngine();
    for (const { cleanup } of instances) {
      await cleanup();
    }
  }

  Assert.ok(true);
  reportMetrics(combined);
}
