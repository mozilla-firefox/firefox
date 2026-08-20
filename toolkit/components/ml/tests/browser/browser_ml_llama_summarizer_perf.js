/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const rootDataUrl =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/articles";

async function fetchArticle(url) {
  const response = await fetch(url);
  return await response.text();
}

let testData = [];

// Both models run the configuration consumers actually ship: llama.cpp with
// neither mmap nor mlock, as LinkPreviewModel creates its engine. Options the
// engine does not honor are deliberately absent.
const smollm2Model = {
  taskName: "text-generation",
  modelId: "HuggingFaceTB/SmolLM2-360M-Instruct-GGUF",
  modelFile: "smollm2-360m-instruct-q8_0.gguf",
  useMmap: false,
  useMlock: false,
  perfModelId: "HuggingFaceTB/SmolLM2-360M-Instruct",
  backend: "llama.cpp",
};

const qwen3Model = {
  taskName: "text-generation",
  modelId: "unsloth/Qwen3-0.6B-GGUF",
  modelFile: "Qwen3-0.6B-Q8_0.gguf",
  useMmap: false,
  useMlock: false,
  perfModelId: "unsloth/Qwen3-0.6B-GGUF",
  backend: "llama.cpp",
};

// The two ends of the range consumers see; a middle article only interpolates
// between them, at the cost of a third of the runtime.
const articles = [
  {
    data: `${rootDataUrl}/tiny.txt`,
    type: "tiny",
    numTokens: 200,
  },
  {
    data: `${rootDataUrl}/big.txt`,
    type: "big",
    numTokens: 1100,
  },
];

let numEngines = 0;

for (const model of [smollm2Model, qwen3Model]) {
  for (const article of articles) {
    // Replace all non-alphabnumeric or dash or underscore by underscore
    const perfName = `${model.perfModelId.replace(/\//g, "-")}_${article.type}`;

    const engineId = `engine-${numEngines}`;

    const options = {
      ...model,
      article: article.data,
      engineId,
      perfName,
      numTokens: article.numTokens,
    };

    numEngines += 1;

    testData.push(options);
  }
}

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_ml_llama_summarizer_perf.js",
  description:
    "Summarization latency and memory for the SmolLM2 and Qwen3 models on llama.cpp",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        {
          name: "latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "memory",
          unit: "MB",
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
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

requestLongerTimeout(30);

// To run locally
// pip install huggingface-hub
// huggingface-cli download {model_id} --local-dir MOZ_ML_LOCAL_DIR/onnx-models/{model_id}/{revision}

// Update your test in
// Then run:  ./mach perfdocs --generate
// This will auto-generate docs
async function run_summarizer_with_perf({
  taskName,
  modelId,
  article,
  engineId,
  perfName,
  numTokens,
  trackPeakMemory,
  ...llamaOptions
}) {
  let chatInput = await fetchArticle(article);

  const numNewTokens = 80;

  const nextPowerOf2 = n => {
    if (n <= 1) {
      return 1;
    }
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
  };

  const numContext = nextPowerOf2(numTokens + numNewTokens + 10);

  const options = new PipelineOptions({
    engineId,
    taskName,
    modelHubUrlTemplate: "{model}/{revision}",
    modelId,
    modelRevision: "main",
    numContext,
    backend: "llama.cpp",
    timeoutMS: -1,
    ...llamaOptions,
  });

  console.log("detected concurrency", navigator.hardwareConcurrency);

  if (taskName.includes("text-generation")) {
    chatInput = [
      {
        role: "system",
        content:
          "Your role is to summarize the provided content as succinctly as possible while retaining the most important information",
      },
      {
        role: "user",
        content: chatInput,
      },
    ];
  }

  const request = {
    prompt: chatInput,
    nPredict: numNewTokens,
    skipPrompt: false,
    stopOnEndOfGenerationTokens: false,
    context: { swaFull: false, flashAttn: false },
  };

  await runMLPerfTest({
    name: `sum-${perfName}`,
    options,
    request,
    trackPeakMemory,
    // Not an ONNX consumer: pin the backend so the matrix does not apply.
    backends: ["llama.cpp"],
  });
}

// Generated from testData so adding a model or an article cannot silently
// reuse a task name, and so no task hardcodes an index into it.
for (const data of testData) {
  for (const trackPeakMemory of [false, true]) {
    const name = `test_summarizer_${data.perfName.replace(/\W/g, "_")}${
      trackPeakMemory ? "_with_mem" : ""
    }`;
    add_task(
      {
        async [name]() {
          await run_summarizer_with_perf({ ...data, trackPeakMemory });
        },
      }[name]
    );
  }
}
