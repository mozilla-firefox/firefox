/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that model PipelineOptions can override the defaults.
 */
add_task(async function test_ml_engine_override_options() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine({
    taskName: "moz-echo",
    modelRevision: "v1",
  });

  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.equal(
    (await inferencePromise).output.modelRevision,
    "v1",
    "The config options goes through and overrides."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Verify that features such as the dtype can be picked up via Remote Settings.
 */
add_task(async function test_ml_engine_pick_feature_id() {
  // one record sent back from RS contains featureId
  const records = [
    {
      taskName: "moz-echo",
      modelId: "mozilla/distilvit",
      processorId: "mozilla/distilvit",
      tokenizerId: "mozilla/distilvit",
      modelRevision: "main",
      processorRevision: "main",
      tokenizerRevision: "main",
      dtype: "q8",
      id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
    },
    {
      featureId: "pdfjs-alt-text",
      taskName: "moz-echo",
      modelId: "mozilla/distilvit",
      processorId: "mozilla/distilvit",
      tokenizerId: "mozilla/distilvit",
      modelRevision: "v1.0",
      processorRevision: "v1.0",
      tokenizerRevision: "v1.0",
      dtype: "fp16",
      id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
    },
  ];

  const { cleanup, remoteClients } = await setup({ records });

  info("Get the engine");
  const engineInstance = await createEngine({
    featureId: "pdfjs-alt-text",
    taskName: "moz-echo",
  });

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

  Assert.equal(
    res.output.dtype,
    "fp16",
    "The config was enriched by RS - using a feature Id"
  );
  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

/**
 * Tests the generic pipeline API
 */
add_task(async function test_ml_generic_pipeline() {
  const { cleanup, remoteClients } = await setup();

  info("Get engineInstance");

  const options = new PipelineOptions({
    taskName: "summarization",
    modelId: "test-echo",
    modelRevision: "main",
  });

  const engineInstance = await createEngine(options);

  info("Run the inference");
  const inferencePromise = engineInstance.run({
    args: ["This gets echoed."],
  });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Test out the default precision values.
 */
add_task(async function test_q8_by_default() {
  const { cleanup, remoteClients } = await setup();

  info("Get the engine");
  const engineInstance = await createEngine({
    taskName: "moz-echo",
    modelId: "Xenova/distilbart-cnn-6-6",
    modelHub: "huggingface",
  });

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text gets echoed exercising the whole flow."
  );

  Assert.equal(
    (await inferencePromise).output.dtype,
    "q8",
    "dtype should be set to q8"
  );

  // the model hub sets the revision
  Assert.equal(
    (await inferencePromise).output.modelRevision,
    "main",
    "modelRevision should be main"
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Test that the preference override options only work for the SAFE_OVERRIDE_OPTIONS
 * defined in MLEngineChild.sys.mjs
 */
add_task(
  async function test_override_ml_engine_pipeline_options_in_allow_list() {
    const { cleanup, remoteClients } = await setup();
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "browser.ml.overridePipelineOptions",
          '{"about-inference": {"modelRevision": "v0.2.0"}}',
        ],
      ],
    });

    info("Get the engine");
    const engineInstance = await createEngine({
      taskName: "moz-echo",
      featureId: "about-inference",
    });

    info("Check the inference process is running");
    Assert.equal(await checkForRemoteType("inference"), true);

    info("Run the inference");
    const inferencePromise = engineInstance.run({ data: "This gets echoed." });

    info("Wait for the pending downloads.");
    await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

    Assert.equal(
      (await inferencePromise).output.echo,
      "This gets echoed.",
      "The text get echoed exercising the whole flow."
    );

    Assert.equal(
      (await inferencePromise).output.modelRevision,
      "v0.2.0",
      "The config options goes through and overrides."
    );

    ok(
      !EngineProcess.areAllEnginesTerminated(),
      "The engine process is still active."
    );

    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
);

add_task(async function test_override_ml_pipeline_options_not_in_allow_list() {
  const { cleanup, remoteClients } = await setup();
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.ml.overridePipelineOptions",
        '{"about-inferences": {"modelRevision": "v0.2.0"}}',
      ],
    ],
  });

  info("Get the engine");
  const engineInstance = await createEngine({
    taskName: "moz-echo",
    featureId: "about-inference",
  });
  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.equal(
    (await inferencePromise).output.modelRevision,
    "main",
    "The config options goes through and overrides."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Test that an unsanctioned modelId does not get used.
 */
add_task(async function test_override_ml_pipeline_options_unsafe_options() {
  const { cleanup, remoteClients } = await setup();
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.ml.overridePipelineOptions",
        '{"about-inference": {"modelRevision": "v0.2.0", "modelId": "unsafe-model-id"}}',
      ],
    ],
  });

  info("Get the engine");
  const engineInstance = await createEngine({
    taskName: "moz-echo",
    featureId: "about-inference",
  });

  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  Assert.equal(
    (await inferencePromise).output.echo,
    "This gets echoed.",
    "The text get echoed exercising the whole flow."
  );

  Assert.equal(
    (await inferencePromise).output.modelRevision,
    "v0.2.0",
    "The config options goes through and overrides."
  );

  Assert.equal(
    (await inferencePromise).output.modelId,
    "mozilla/distilvit",
    "The config should not override."
  );

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * Check that DEFAULT_MODELS are used to pick a preferred model for a given task.
 */
add_task(async function test_ml_engine_blessed_model() {
  const { cleanup, remoteClients } = await setup();

  const options = { taskName: "test-echo" };
  const engineInstance = await createEngine(options);

  info("Check the inference process is running");
  Assert.equal(await checkForRemoteType("inference"), true);

  info("Run the inference");
  const inferencePromise = engineInstance.run({ data: "This gets echoed." });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  const res = await inferencePromise;

  Assert.equal(
    res.config.modelId,
    "test-echo",
    "The blessed model was picked."
  );

  Assert.equal(res.config.dtype, "q8", "With the right quantization level");

  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

add_task(async function test_ml_engine_two_tasknames_in_rs() {
  // RS has two records with the same taskName
  // we should use the modelId match in that case
  const records = [
    {
      taskName: "moz-echo",
      modelId: "mozilla/anothermodel",
      processorId: "mozilla/distilvit",
      tokenizerId: "mozilla/distilvit",
      modelRevision: "main",
      processorRevision: "main",
      tokenizerRevision: "main",
      dtype: "q8",
      id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
    },
    {
      taskName: "moz-echo",
      modelId: "mozilla/distilvit",
      processorId: "mozilla/distilvit",
      tokenizerId: "mozilla/distilvit",
      modelRevision: "v1.0",
      processorRevision: "v1.0",
      tokenizerRevision: "v1.0",
      dtype: "fp16",
      id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
    },
  ];

  const { cleanup, remoteClients } = await setup({ records });

  info("Get the engine");
  const engineInstance = await createEngine({
    featureId: "pdfjs-alt-text",
    taskName: "moz-echo",
  });

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

  Assert.equal(
    res.output.dtype,
    "fp16",
    "The config was enriched by RS - using a feature Id"
  );
  ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engine process is still active."
  );

  await EngineProcess.destroyMLEngine();

  await cleanup();
});

/**
 * The modelHub should be applied to the PipelineOptions
 */
add_task(async function test_ml_engine_model_hub_applied() {
  const options = {
    taskName: "moz-echo",
    timeoutMS: -1,
    modelHub: "huggingface",
  };
  const parsedOptions = new PipelineOptions(options);

  Assert.equal(
    parsedOptions.modelHubRootUrl,
    "https://huggingface.co/",
    "modelHubRootUrl is set"
  );

  Assert.equal(
    parsedOptions.modelHubUrlTemplate,
    "{model}/resolve/{revision}",
    "modelHubUrlTemplate is set"
  );
});

/**
 * Helper function to create a basic set of valid options
 */
function getValidOptions(overrides = {}) {
  return Object.assign(
    {
      engineId: "validEngine1",
      featureId: "pdfjs-alt-text",
      taskName: "valid_task",
      modelHubRootUrl: "https://example.com",
      modelHubUrlTemplate: "https://example.com/{modelId}",
      timeoutMS: 5000,
      modelId: "validModel",
      modelRevision: "v1",
      tokenizerId: "validTokenizer",
      tokenizerRevision: "v1",
      processorId: "validProcessor",
      processorRevision: "v1",
      logLevel: null,
      runtimeFilename: "runtime.wasm",
      device: InferenceDevice.GPU,
      numThreads: 4,
      executionPriority: ExecutionPriority.NORMAL,
    },
    overrides
  );
}

/**
 * A collection of test cases for invalid and valid values.
 */
const commonInvalidCases = [
  { description: "Invalid value (special characters)", value: "org1/my!value" },
  {
    description: "Invalid value (special characters in organization)",
    value: "org@1/my-value",
  },
  { description: "Invalid value (missing name part)", value: "org1/" },
  {
    description: "Invalid value (invalid characters in name)",
    value: "my$value",
  },
];

const commonValidCases = [
  { description: "Valid organization/name", value: "org1/my-value" },
  { description: "Valid name only", value: "my-value" },
  {
    description: "Valid name with underscores and dashes",
    value: "my_value-123",
  },
  {
    description: "Valid organization with underscores and dashes",
    value: "org_123/my-value",
  },
];

const pipelineOptionsCases = [
  // Invalid cases for various fields
  ...commonInvalidCases.map(test => ({
    description: `Invalid processorId (${test.description})`,
    options: { processorId: test.value },
    expectedError: /Invalid value/,
  })),
  ...commonInvalidCases.map(test => ({
    description: `Invalid tokenizerId (${test.description})`,
    options: { tokenizerId: test.value },
    expectedError: /Invalid value/,
  })),
  ...commonInvalidCases.map(test => ({
    description: `Invalid modelId (${test.description})`,
    options: { modelId: test.value },
    expectedError: /Invalid value/,
  })),

  // Valid cases for various fields
  ...commonValidCases.map(test => ({
    description: `Valid processorId (${test.description})`,
    options: { processorId: test.value },
    expected: { processorId: test.value },
  })),
  ...commonValidCases.map(test => ({
    description: `Valid tokenizerId (${test.description})`,
    options: { tokenizerId: test.value },
    expected: { tokenizerId: test.value },
  })),
  ...commonValidCases.map(test => ({
    description: `Valid modelId (${test.description})`,
    options: { modelId: test.value },
    expected: { modelId: test.value },
  })),

  // Invalid values
  {
    description: "Invalid hub",
    options: { modelHub: "rogue" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid timeoutMS",
    options: { timeoutMS: -3 },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid timeoutMS",
    options: { timeoutMS: 40000000 },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid featureId",
    options: { featureId: "unknown" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid dtype",
    options: { dtype: "invalid_dtype" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid device",
    options: { device: "invalid_device" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid executionPriority",
    options: { executionPriority: "invalid_priority" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid logLevel",
    options: { logLevel: "invalid_log_level" },
    expectedError: /Invalid value/,
  },

  // Valid values
  {
    description: "valid hub",
    options: { modelHub: "huggingface" },
    expected: { modelHub: "huggingface" },
  },
  {
    description: "valid hub",
    options: { modelHub: "mozilla" },
    expected: { modelHub: "mozilla" },
  },
  {
    description: "valid timeoutMS",
    options: { timeoutMS: 12345 },
    expected: { timeoutMS: 12345 },
  },
  {
    description: "valid timeoutMS",
    options: { timeoutMS: -1 },
    expected: { timeoutMS: -1 },
  },

  {
    description: "Valid dtype",
    options: { dtype: QuantizationLevel.FP16 },
    expected: { dtype: QuantizationLevel.FP16 },
  },
  {
    description: "Valid device",
    options: { device: InferenceDevice.WASM },
    expected: { device: InferenceDevice.WASM },
  },
  {
    description: "Valid executionPriority",
    options: { executionPriority: ExecutionPriority.HIGH },
    expected: { executionPriority: ExecutionPriority.HIGH },
  },
  {
    description: "Valid logLevel (Info)",
    options: { logLevel: LogLevel.INFO },
    expected: { logLevel: LogLevel.INFO },
  },
  {
    description: "Valid logLevel (Critical)",
    options: { logLevel: LogLevel.CRITICAL },
    expected: { logLevel: LogLevel.CRITICAL },
  },
  {
    description: "Valid logLevel (All)",
    options: { logLevel: LogLevel.ALL },
    expected: { logLevel: LogLevel.ALL },
  },
  {
    description: "Valid modelId",
    options: { modelId: "Qwen2.5-0.5B-Instruct" },
    expected: { modelId: "Qwen2.5-0.5B-Instruct" },
  },

  // Invalid revision cases
  {
    description: "Invalid revision (random string)",
    options: { modelRevision: "invalid_revision" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid revision (too many version numbers)",
    options: { tokenizerRevision: "v1.0.3.4.5" },
    expectedError: /Invalid value/,
  },
  {
    description: "Invalid revision (unknown suffix)",
    options: { processorRevision: "v1.0.0-unknown" },
    expectedError: /Invalid value/,
  },

  // Valid revision cases with new format
  {
    description: "Valid revision (main)",
    options: { modelRevision: "main" },
    expected: { modelRevision: "main" },
  },
  {
    description: "Valid revision (v-prefixed version with alpha)",
    options: { tokenizerRevision: "v1.2.3-alpha1" },
    expected: { tokenizerRevision: "v1.2.3-alpha1" },
  },
  {
    description:
      "Valid revision (v-prefixed version with beta and dot separator)",
    options: { tokenizerRevision: "v1.2.3.beta2" },
    expected: { tokenizerRevision: "v1.2.3.beta2" },
  },
  {
    description:
      "Valid revision (non-prefixed version with rc and dash separator)",
    options: { processorRevision: "1.0.0-rc3" },
    expected: { processorRevision: "1.0.0-rc3" },
  },
  {
    description:
      "Valid revision (non-prefixed version with pre and dot separator)",
    options: { processorRevision: "1.0.0.pre4" },
    expected: { processorRevision: "1.0.0.pre4" },
  },
  {
    description: "Valid revision (version without suffix)",
    options: { modelRevision: "1.0.0" },
    expected: { modelRevision: "1.0.0" },
  },

  // Valid engineID cases
  {
    description: "Valid engineID (qwen)",
    options: { engineId: "SUM-ONNX-COMMUNITY_QWEN2_5-0_5B-INSTRUCT_BIG" },
    expected: { engineId: "SUM-ONNX-COMMUNITY_QWEN2_5-0_5B-INSTRUCT_BIG" },
  },
];

/**
 * Go through all of the pipeline validation test cases.
 */
add_task(async function test_pipeline_options_validation() {
  pipelineOptionsCases.forEach(testCase => {
    if (testCase.expectedError) {
      Assert.throws(
        () => new PipelineOptions(getValidOptions(testCase.options)),
        testCase.expectedError,
        `${testCase.description} throws the expected error`
      );
    } else {
      const pipelineOptions = new PipelineOptions(
        getValidOptions(testCase.options)
      );
      Object.keys(testCase.expected).forEach(key => {
        is(
          pipelineOptions[key],
          testCase.expected[key],
          `${testCase.description} sets ${key} correctly`
        );
      });
    }
  });
});

/**
 * The pipeline should only be able to be initialized when there is enough memory.
 */
add_task(async function test_ml_engine_not_enough_memory() {
  const { cleanup } = await setup({
    prefs: [
      ["browser.ml.checkForMemory", true],
      ["browser.ml.minimumPhysicalMemory", 99999],
    ],
  });

  info("Get the greedy engine");

  await Assert.rejects(
    createEngine({
      modelId: "testing/greedy",
      taskName: "moz-echo",
      dtype: "q8",
      numThreads: 1,
      device: "wasm",
    }),
    /Not enough physical memory/,
    "The call should be rejected because of a lack of memory"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

/**
 * This tests that threading is supported. On certain machines this could be false,
 * but should be true for our testing infrastructure.
 */
add_task(async function test_ml_threading_support() {
  const { cleanup, remoteClients } = await setup();

  info("Get engineInstance");

  const options = new PipelineOptions({
    taskName: "summarization",
    modelId: "test-echo",
    modelRevision: "main",
  });

  const engineInstance = await createEngine(options);

  info("Run the inference");
  const inferencePromise = engineInstance.run({
    args: ["This gets echoed."],
  });

  info("Wait for the pending downloads.");
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);

  let res = await inferencePromise;

  ok(res.multiThreadSupported, "Multi-thread should be supported");
  await EngineProcess.destroyMLEngine();
  await cleanup();
});
