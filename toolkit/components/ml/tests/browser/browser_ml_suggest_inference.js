/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const PREFIX = "inference";

// This test checks inference output rather than timings, but perfherder needs
// a metric per run to accept the job, so it reports placeholders.
function placeholderMetrics(tag) {
  const journal = {};
  for (const metric of [
    PIPELINE_READY_LATENCY,
    INITIALIZATION_LATENCY,
    MODEL_RUN_LATENCY,
    TOTAL_MEMORY_USAGE,
  ]) {
    journal[`${PREFIX}-${metric}-${tag}`] = [1];
  }
  return journal;
}

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_ml_suggest_inference.js",
  description: "Template test for ML suggest Inference Model",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        {
          name: "inference-pipeline-ready-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "inference-initialization-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "inference-model-run-latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "inference-total-memory-usage",
          unit: "MiB",
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

const CUSTOM_INTENT_OPTIONS = {
  taskName: "text-classification",
  featureId: "suggest-intent-classification",
  engineId: "ml-suggest-intent",
  modelId: "Mozilla/mobilebert-uncased-finetuned-LoRA-intent-classifier",
  modelHubUrlTemplate: "{model}/{revision}",
  dtype: "q8",
  modelRevision: "main",
  numThreads: 2,
  timeoutMS: -1,
};

const CUSTOM_NER_OPTIONS = {
  taskName: "token-classification",
  featureId: "suggest-NER",
  engineId: "ml-suggest-ner",
  modelId: "Mozilla/distilbert-uncased-NER-LoRA",
  modelHubUrlTemplate: "{model}/{revision}",
  dtype: "q8",
  modelRevision: "main",
  numThreads: 2,
  timeoutMS: -1,
};

const ROOT_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/suggest";
const YELP_KEYWORDS_DATA = `${ROOT_URL}/yelp_val_keywords_data.json`;
const YELP_VAL_DATA = `${ROOT_URL}/yelp_val_generated_data.json`;
const NER_VAL_DATA = `${ROOT_URL}/named_entity_val_generated_data.json`;

async function get_val_data(inputDataPath) {
  const response = await fetch(inputDataPath);
  if (!response.ok) {
    throw new Error(
      `Failed to fetch data: ${response.statusText} from ${inputDataPath}`
    );
  }
  return response.json();
}

async function read_data_by_type(type) {
  let data;
  if (type === "YELP_KEYWORDS_DATA") {
    data = await get_val_data(YELP_KEYWORDS_DATA);
    return data[0].subjects;
  } else if (type === "YELP_VAL_DATA") {
    data = await get_val_data(YELP_VAL_DATA);
    return data.queries;
  } else if (type === "NER_VAL_DATA") {
    data = await get_val_data(NER_VAL_DATA);
    return data.queries;
  }
  return [];
}

// Utility to write results to a local JSON file using IOUtils
async function writeResultsToFile(results, type) {
  try {
    const json = JSON.stringify(results, null, 2);
    const OUTPUT_FILE_PATH = normalizePathForOS(
      `${
        Services.dirsvc.get("DfltDwnld", Ci.nsIFile).path
      }/ML_output_${type}.json`
    );
    await IOUtils.writeUTF8(OUTPUT_FILE_PATH, json);
    console.log("Results successfully written to:", OUTPUT_FILE_PATH);
  } catch (error) {
    console.error("Failed to write results to file:", error);
    Assert.ok(false, "Failed to write results to file");
  }
}

async function perform_inference(queries, type) {
  // Ensure MLSuggest is initialized
  await MLSuggest.initialize();
  const results = [];
  for (const query of queries) {
    const suggestion = await MLSuggest.makeSuggestions(query);
    const res = {
      query,
      intent: suggestion.intent,
      city: suggestion.location.city,
      state: suggestion.location.state,
    };
    results.push(res);
  }

  Assert.strictEqual(
    results.length,
    queries.length,
    "results size should be equal to queries size."
  );
  // Write results to a file
  await writeResultsToFile(results, type);
}

const runInference2 = async ({ backend, tag }) => {
  ChromeUtils.defineESModuleGetters(this, {
    MLSuggest: "moz-src:///browser/components/urlbar/private/MLSuggest.sys.mjs",
  });

  // Pin the fallbacks too: MLSuggest falls back from "onnx-native" to
  // "onnx" on its own, which would report wasm numbers under the native tag.
  MLSuggest.INTENT_OPTIONS = { ...CUSTOM_INTENT_OPTIONS, backend };
  MLSuggest.INTENT_OPTIONS_FALLBACK = { ...CUSTOM_INTENT_OPTIONS, backend };
  MLSuggest.NER_OPTIONS = { ...CUSTOM_NER_OPTIONS, backend };
  MLSuggest.NER_OPTIONS_FALLBACK = { ...CUSTOM_NER_OPTIONS, backend };

  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  if (!modelHubRootUrl) {
    throw new Error(
      "MOZ_MODELS_HUB is not set, you need to run with --hooks toolkit/components/ml/tests/tools/hook_local_hub.py"
    );
  }
  info(`ModelHubRootUrl: ${modelHubRootUrl}`);
  const { cleanup } = await perfSetup({
    prefs: [
      ["browser.ml.modelHubRootUrl", modelHubRootUrl],
      ["javascript.options.wasm_lazy_tiering", true],
    ],
  });

  const TYPES = ["YELP_KEYWORDS_DATA", "YELP_VAL_DATA", "NER_VAL_DATA"];
  try {
    for (const type of TYPES) {
      info(`processing ${type} now`);
      // Read data for the current type
      const queries = await read_data_by_type(type);
      if (!queries) {
        info(`No queries found for type: ${type}`);
        continue;
      }
      // Run inference for each query
      await perform_inference(queries, `${type}-${tag}`);
    }
  } finally {
    // The next backend needs a clean MLSuggest and pref state.
    await MLSuggest.shutdown();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
  reportMetrics(placeholderMetrics(tag));
};

/**
 * Tests remote ml model
 */
add_task(async function test_ml_generic_pipeline() {
  await runMLPerfTestForEachBackend({ name: PREFIX, run: runInference2 });
});
