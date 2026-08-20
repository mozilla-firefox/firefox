/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_ml_smart_tab_perf.js",
  description: "Testing Smart Tab Models",
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

requestLongerTimeout(10);

// Topic model tests
add_task(async function test_ml_smart_tab_topic() {
  const options = new PipelineOptions({
    taskName: "text2text-generation",
    modelId: "Mozilla/smart-tab-topic",
    modelHubUrlTemplate: "{model}/{revision}",
    modelRevision: "main",
    dtype: "q8",
    timeoutMS: 2 * 60 * 1000,
  });

  const requestInfo = {
    inputArgs:
      "Vintage Cameras and Their Impact The First Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections",
    runOptions: {
      max_length: 6,
    },
  };

  const request = {
    args: [requestInfo.inputArgs],
    options: requestInfo.runOptions,
  };

  await runMLPerfTest({
    name: "smart-tab-topic",
    options,
    request,
    iterations: 2,
    addColdStart: true,
    trackPeakMemory: true,
  });

  await runMLPerfTest({
    name: "smart-tab-topic",
    options,
    request,
    iterations: 2,
    addColdStart: true,
    trackPeakMemory: false,
  });
});

// Embedding model tests
async function testEmbedding(trackPeakMemory = false) {
  const options = new PipelineOptions({
    taskName: "feature-extraction",
    modelId: "Mozilla/smart-tab-embedding",
    modelHubUrlTemplate: "{model}/{revision}",
    modelRevision: "main",
    dtype: "q8",
    timeoutMS: 2 * 60 * 1000,
  });

  var input;

  if (trackPeakMemory) {
    input = [
      "Vintage Cameras and Their Impact The First Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections",
      "Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact",
      "The Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact on tech.",
      "Vintage Cameras and Their Impact The First Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections",
      "Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact",
      "The Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact on tech.",
      "Vintage Cameras and Their Impact The First Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections",
      "Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact",
      "The Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact on tech.",
    ];
  } else {
    input = [
      "Vintage Cameras and Their Impact The First Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections",
      "Personal Computers Discussing the Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact",
      "The Evolution of Smartphones 80s Tech Recollections Vintage Cameras and Their Impact on tech.",
    ];
  }

  const requestInfo = {
    inputArgs: input,
    runOptions: {
      pooling: "mean",
      normalize: true,
    },
  };

  const request = {
    args: [requestInfo.inputArgs],
    options: requestInfo.runOptions,
  };

  await runMLPerfTest({
    name: "smart-tab-embedding",
    options,
    request,
    iterations: 2,
    addColdStart: true,
    trackPeakMemory,
    peakMemoryInterval: 10,
  });
}

add_task(async function test_ml_smart_tab_embedding() {
  await testEmbedding(false);
});

add_task(async function test_ml_smart_tab_embedding() {
  await testEmbedding(true);
});
