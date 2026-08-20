/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const ENGINES = {
  intent: {
    engineId: "intent",
    taskName: "text-classification",
    modelId: "Mozilla/mobilebert-uncased-finetuned-LoRA-intent-classifier",
    modelRevision: "main",
    modelHubUrlTemplate: "{model}/{revision}",
    dtype: "q8",
    device: "wasm",
    request: {
      args: [["restaurants in seattle, wa"]],
    },
  },
  suggest: {
    engineId: "suggest",
    taskName: "token-classification",
    modelId: "Mozilla/distilbert-uncased-NER-LoRA",
    modelRevision: "main",
    dtype: "q8",
    device: "wasm",
    request: {
      args: [["restaurants in seattle, wa"]],
    },
  },
  engine3: {
    engineId: "engine3",
    taskName: "feature-extraction",
    modelId: "Xenova/all-MiniLM-L6-v2",
    modelRevision: "main",
    dtype: "q8",
    device: "wasm",
    request: {
      args: [["Yet another example sentence", "Checking sentence handling"]],
      options: {
        pooling: "mean",
        normalize: true,
      },
    },
  },
  engine4: {
    engineId: "engine4",
    taskName: "feature-extraction",
    modelId: "Xenova/all-MiniLM-L6-v2",
    modelRevision: "main",
    dtype: "q8",
    device: "wasm",
    request: {
      args: [["Final example sentence", "Ensuring unique inputs"]],
      options: {
        pooling: "mean",
        normalize: true,
      },
    },
  },
};

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_ml_engine_multi_perf.js",
  description: "Testing model execution concurrently",
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

async function runEngineWithMetrics(
  engineInstance,
  engineConfig,
  iterations = 1,
  tag
) {
  const journal = {};
  const engine = engineInstance.engine;
  for (let i = 0; i < iterations; i++) {
    const res = await engine.run(engineConfig.request);
    let metrics = fetchMetrics(res.metrics);

    // Collect metrics, prefixing each metric name with engineId and backend
    for (const [metricName, metricVal] of Object.entries(metrics)) {
      const prefixedMetricName = `${engineConfig.engineId}-${metricName}-${tag}`;
      if (!journal[prefixedMetricName]) {
        journal[prefixedMetricName] = [];
      }
      journal[prefixedMetricName].push(metricVal);
    }
  }
  return journal;
}

/**
 * Runs inference on an initialized engine instance using the specified request configuration
 * and collects metrics, prefixed with the engineId.
 *
 * @param {object} engineInstance - The engine instance on which to run inference.
 * @param {EngineConfig} engineConfig - Configuration object with request details for the engine.
 * @param {number} iterations - Number of times to run the inference for metrics collection.
 * @returns {Promise<object>} - Returns a promise that resolves with the journal of collected metrics.
 */

/**
 * Tests concurrent execution of the ml pipeline API by starting engines first, then running inference.
 */
add_task(async function test_ml_generic_pipeline_concurrent_separate_phases() {
  await runMLPerfTestForEachBackend({
    name: "MULTI",
    run: runConcurrentEngines,
  });
});

async function runConcurrentEngines({ backend, tag }) {
  // Step 1: Initialize all engines concurrently
  const engineInstances = await Promise.all(
    Object.values(ENGINES).map(async engineConfig => {
      const { cleanup, engine } = await initializeEngine(
        new PipelineOptions({ timeoutMS: -1, ...engineConfig, backend })
      );
      return { cleanup, engine };
    })
  );
  info("All engines initialized successfully");

  let combinedJournal;
  try {
    // Step 2: Run inference on all initialized engines concurrently and collect metrics
    const allJournals = await Promise.all(
      engineInstances.map((engineInstance, index) =>
        runEngineWithMetrics(
          engineInstance,
          Object.values(ENGINES)[index],
          ITERATIONS,
          tag
        )
      )
    );

    // Merge all journals into one for final reporting
    combinedJournal = allJournals.reduce((acc, journal) => {
      Object.entries(journal).forEach(([key, values]) => {
        if (!acc[key]) {
          acc[key] = [];
        }
        acc[key].push(...values);
      });
      return acc;
    }, {});

    Assert.ok(true);

    const memUsage = await getTotalMemoryUsage();
    (combinedJournal[`MULTI-total-memory-usage-${tag}`] =
      combinedJournal[`MULTI-total-memory-usage-${tag}`] || []).push(memUsage);
  } finally {
    await EngineProcess.destroyMLEngine();

    // Cleanup and verify that all engines are terminated
    for (const instance of engineInstances) {
      await instance.cleanup();
    }
  }

  // Final metrics report
  reportMetrics(combinedJournal);
}
